// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "BPDirectExporter.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimationCustomTransitionGraph.h"
#include "AnimationConduitGraphSchema.h"
#include "Engine/SkeletalMesh.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_BlendListByEnum.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateAliasNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "K2Node.h"
#include "K2Node_AnimGetter.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_TransitionRuleGetter.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Knot.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Timeline.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Composite.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_Message.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_Select.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Self.h"
#include "K2Node_Tunnel.h"
#include "UObject/UnrealType.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_AssignDelegate.h"
#include "K2Node_InputKey.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionTerminator.h"
#include "Components/SceneComponent.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "InputAction.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/MetaData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "EdGraphSchema_K2.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "StructUtils/InstancedStruct.h"
#include "EditorAssetLibrary.h"

namespace
{
FString MakePythonStringLiteral_ExportBpy(const FString& Text);
FString BuildPinRefAttribute_ExportBpy(const FString& NodeVar, const FString& RawPinName, bool bAllowGuidCleanup);

void AddNodePropertyTextIfPresent_ExportBpy(UK2Node* Node, FNodeInfo& Info, const TCHAR* PropertyName)
{
	if (!Node || !PropertyName)
	{
		return;
	}

	FProperty* Property = Node->GetClass()->FindPropertyByName(FName(PropertyName));
	if (!Property)
	{
		return;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
	if (!ValuePtr)
	{
		return;
	}

	FString ExportedValue;
	Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, Node, PPF_None);
	if (!ExportedValue.IsEmpty())
	{
		Info.NodeProps.Add(PropertyName, ExportedValue);
	}
}

void AddNodePropertyDeltaTextIfPresent_ExportBpy(UK2Node* Node, FNodeInfo& Info, const TCHAR* PropertyName)
{
	if (!Node || !PropertyName)
	{
		return;
	}

	FProperty* Property = Node->GetClass()->FindPropertyByName(FName(PropertyName));
	if (!Property)
	{
		return;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
	if (!ValuePtr)
	{
		return;
	}

	void* DefaultPtr = nullptr;
	if (UObject* DefaultObject = Node->GetClass()->GetDefaultObject())
	{
		DefaultPtr = Property->ContainerPtrToValuePtr<void>(DefaultObject);
	}

	FString ExportedValue;
	Property->ExportTextItem_Direct(ExportedValue, ValuePtr, DefaultPtr, Node, PPF_None);
	if (!ExportedValue.IsEmpty())
	{
		Info.NodeProps.Add(PropertyName, ExportedValue);
	}
}

void AddNodePropertyDeltaOrTextIfPresent_ExportBpy(UK2Node* Node, FNodeInfo& Info, const TCHAR* PropertyName)
{
	if (!Node || !PropertyName)
	{
		return;
	}

	AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, PropertyName);
	if (Info.NodeProps.Contains(PropertyName))
	{
		return;
	}

	AddNodePropertyTextIfPresent_ExportBpy(Node, Info, PropertyName);
}

FString GetOriginalChooserAssetPathForExport_ExportBpy(const UObject* Object)
{
	if (!Object)
	{
		return FString();
	}

	UPackage* Package = Object->GetOutermost();
	if (!Package)
	{
		return FString();
	}

	FMetaData& MetaData = Package->GetMetaData();
	const FString SourcePath = MetaData.GetValue(Object, TEXT("ExportBpy.SourceChooserAssetPath"));
	if (!SourcePath.StartsWith(TEXT("/")))
	{
		return FString();
	}

	if (SourcePath.Contains(TEXT("'")))
	{
		return SourcePath;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(SourcePath);
	if (AssetName.IsEmpty())
	{
		return SourcePath;
	}

	return FString::Printf(
		TEXT("/Script/Chooser.ChooserTable'%s.%s'"),
		*SourcePath,
		*AssetName);
}

void AddNodeObjectPropertyTextIfPresent_ExportBpy(UK2Node* Node, FNodeInfo& Info, const TCHAR* PropertyName)
{
	if (!Node || !PropertyName)
	{
		return;
	}

	AddNodePropertyTextIfPresent_ExportBpy(Node, Info, PropertyName);
	if (const FString* ExportedValue = Info.NodeProps.Find(PropertyName))
	{
		if (ExportedValue->Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Info.NodeProps.Remove(PropertyName);
		}
	}
}

void AddChooserPropertyTextIfPresent_ExportBpy(UK2Node* Node, FNodeInfo& Info)
{
	if (!Node)
	{
		return;
	}

	AddNodeObjectPropertyTextIfPresent_ExportBpy(Node, Info, TEXT("Chooser"));

	const FObjectPropertyBase* ChooserProperty =
		FindFProperty<FObjectPropertyBase>(Node->GetClass(), FName(TEXT("Chooser")));
	if (!ChooserProperty)
	{
		return;
	}

	const UObject* ChooserObject = ChooserProperty->GetObjectPropertyValue_InContainer(Node);
	const FString SourceChooserPath = GetOriginalChooserAssetPathForExport_ExportBpy(ChooserObject);
	if (!SourceChooserPath.IsEmpty())
	{
		Info.NodeProps.Add(TEXT("Chooser"), SourceChooserPath);
	}
}
bool ExportAnimNodeBindingPropertyBindingsText_ExportBpy(UK2Node* Node, FString& OutExportedValue)
{
	OutExportedValue.Reset();

	if (!Node)
	{
		return false;
	}

	// Prefer Binding subobject storage (UE5.7+). Node-level PropertyBindings can
	// resolve to deprecated storage and must not be used as primary source.
	const FObjectPropertyBase* BindingProperty =
		FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("Binding"));
	if (BindingProperty)
	{
		UObject* BindingObject = BindingProperty->GetObjectPropertyValue_InContainer(Node);
		if (BindingObject)
		{
			FProperty* PropertyBindingsProperty =
				BindingObject->GetClass()->FindPropertyByName(TEXT("PropertyBindings"));
			if (PropertyBindingsProperty)
			{
				void* ValuePtr = PropertyBindingsProperty->ContainerPtrToValuePtr<void>(BindingObject);
				if (ValuePtr)
				{
					void* DefaultPtr = nullptr;
					if (UObject* DefaultNode = Node->GetClass()->GetDefaultObject())
					{
						if (UObject* DefaultBindingObject = BindingProperty->GetObjectPropertyValue_InContainer(DefaultNode))
						{
							DefaultPtr = PropertyBindingsProperty->ContainerPtrToValuePtr<void>(DefaultBindingObject);
						}
					}

					PropertyBindingsProperty->ExportTextItem_Direct(
						OutExportedValue,
						ValuePtr,
						DefaultPtr,
						BindingObject,
						PPF_None);
					if (!OutExportedValue.IsEmpty() && OutExportedValue != TEXT("()"))
					{
						return true;
					}
					OutExportedValue.Reset();
				}
			}
		}
	}

	// Fallback for older branches where bindings are still on the node.
	if (FMapProperty* DirectBindingsProperty =
			FindFProperty<FMapProperty>(Node->GetClass(), TEXT("PropertyBindings")))
	{
		void* ValuePtr = DirectBindingsProperty->ContainerPtrToValuePtr<void>(Node);
		void* DefaultPtr = nullptr;
		if (UObject* DefaultNode = Node->GetClass()->GetDefaultObject(false))
		{
			DefaultPtr = DirectBindingsProperty->ContainerPtrToValuePtr<void>(DefaultNode);
		}

		DirectBindingsProperty->ExportTextItem_Direct(
			OutExportedValue,
			ValuePtr,
			DefaultPtr,
			Node,
			PPF_None);
		if (!OutExportedValue.IsEmpty() && OutExportedValue != TEXT("()"))
		{
			return true;
		}
		OutExportedValue.Reset();
	}

	return false;
}

bool ShouldTraceAnimBindingNode_ExportBpy(const UK2Node* Node)
{
	if (!Node || !Node->GetClass())
	{
		return false;
	}

	const FString NodeClass = Node->GetClass()->GetName();
	return NodeClass == TEXT("AnimGraphNode_MotionMatching") ||
		NodeClass == TEXT("AnimGraphNode_OffsetRootBone");
}

void LogAnimBindingPins_ExportBpy(UK2Node* Node, const TSet<FString>& PinNames, const TCHAR* SourceTag)
{
	if (!ShouldTraceAnimBindingNode_ExportBpy(Node))
	{
		return;
	}

	TArray<FString> SortedPins = PinNames.Array();
	SortedPins.Sort();
	const FString JoinedPins = FString::Join(SortedPins, TEXT(","));

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[ExportBpy][BindingTrace] source=%s node=%s class=%s pin_count=%d pins=[%s]"),
		SourceTag ? SourceTag : TEXT("unknown"),
		*Node->GetName(),
		*Node->GetClass()->GetName(),
		PinNames.Num(),
		*JoinedPins);
}

TSet<FString> CollectAnimNodeBindingDrivenPinNames_ExportBpy(UK2Node* Node)
{
	TSet<FString> Result;

	if (!Node)
	{
		return Result;
	}

	// Prefer binding keys from text export (which itself prefers Binding subobject
	// storage on UE5.7+).
	FString BindingText;
	if (!ExportAnimNodeBindingPropertyBindingsText_ExportBpy(Node, BindingText) || BindingText.IsEmpty())
	{
		return Result;
	}

	static const FString PropertyNameToken = TEXT("PropertyName=\"");
	int32 SearchFrom = 0;
	while (SearchFrom < BindingText.Len())
	{
		const int32 TokenIndex = BindingText.Find(
			PropertyNameToken,
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			SearchFrom);
		if (TokenIndex == INDEX_NONE)
		{
			break;
		}

		const int32 NameStart = TokenIndex + PropertyNameToken.Len();
		const int32 NameEnd = BindingText.Find(
			TEXT("\""),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			NameStart);
		if (NameEnd == INDEX_NONE || NameEnd <= NameStart)
		{
			break;
		}

		Result.Add(BindingText.Mid(NameStart, NameEnd - NameStart));
		SearchFrom = NameEnd + 1;
	}

	LogAnimBindingPins_ExportBpy(Node, Result, TEXT("binding_text"));
	return Result;
}

void AddAnimNodeBindingPropertyBindingsIfPresent_ExportBpy(UK2Node* Node, FNodeInfo& Info)
{
	if (!Node || !Info.NodeType.StartsWith(TEXT("AnimGraphNode_")))
	{
		return;
	}

	FString ExportedValue;
	if (ExportAnimNodeBindingPropertyBindingsText_ExportBpy(Node, ExportedValue))
	{
		Info.NodeProps.Add(TEXT("BindingPropertyBindings"), ExportedValue);
	}
}

/**
 * Strip fields whose values are supplied at runtime by PropertyBindings
 * from an `ExportTextItem_Direct` struct body like
 *     "(A=1.0,B=2.0,C=3.0)"
 * Without this, the exported `Node=(...)` text would carry stale in-memory
 * constants for bound fields; on re-import those constants would be written
 * back into the FAnimNode_* struct, defeating the binding (bound fields
 * would fall back to those constants whenever the ExposedValueHandler
 * fails to kick in).
 *
 * Safe against quoted string values that contain commas, parentheses,
 * or escape sequences by using a depth/quote tracking scanner.
 */
void StripBoundFieldsFromStructText_ExportBpy(
	FString& InOutStructText,
	const TSet<FString>& BoundFieldNames)
{
	if (InOutStructText.IsEmpty() || BoundFieldNames.Num() == 0)
	{
		return;
	}
	if (!InOutStructText.StartsWith(TEXT("(")) || !InOutStructText.EndsWith(TEXT(")")))
	{
		return; // not a struct body we know how to edit
	}

	const FString Body = InOutStructText.Mid(1, InOutStructText.Len() - 2);
	const int32 Len = Body.Len();
	if (Len == 0)
	{
		return;
	}

	TArray<FString> Kept;
	int32 FieldStart = 0;
	int32 Depth = 0;
	bool bInQuotes = false;
	bool bEscapeNext = false;

	auto EmitField = [&](int32 EndExclusive)
	{
		if (EndExclusive <= FieldStart)
		{
			return;
		}
		const FString Entry = Body.Mid(FieldStart, EndExclusive - FieldStart);
		int32 EqIdx = INDEX_NONE;
		// Find '=' at top level, outside quotes
		int32 LocalDepth = 0;
		bool LocalQuotes = false;
		bool LocalEscape = false;
		for (int32 i = 0; i < Entry.Len(); ++i)
		{
			const TCHAR Ch = Entry[i];
			if (LocalEscape) { LocalEscape = false; continue; }
			if (Ch == TEXT('\\')) { LocalEscape = true; continue; }
			if (Ch == TEXT('"')) { LocalQuotes = !LocalQuotes; continue; }
			if (LocalQuotes) continue;
			if (Ch == TEXT('(')) { ++LocalDepth; continue; }
			if (Ch == TEXT(')')) { --LocalDepth; continue; }
			if (Ch == TEXT('=') && LocalDepth == 0) { EqIdx = i; break; }
		}
		if (EqIdx == INDEX_NONE)
		{
			Kept.Add(Entry);
			return;
		}
		const FString Key = Entry.Left(EqIdx).TrimStartAndEnd();
		if (!BoundFieldNames.Contains(Key))
		{
			Kept.Add(Entry);
		}
	};

	for (int32 i = 0; i < Len; ++i)
	{
		const TCHAR Ch = Body[i];
		if (bEscapeNext) { bEscapeNext = false; continue; }
		if (Ch == TEXT('\\')) { bEscapeNext = true; continue; }
		if (Ch == TEXT('"')) { bInQuotes = !bInQuotes; continue; }
		if (bInQuotes) continue;
		if (Ch == TEXT('(')) { ++Depth; continue; }
		if (Ch == TEXT(')')) { --Depth; continue; }
		if (Ch == TEXT(',') && Depth == 0)
		{
			EmitField(i);
			FieldStart = i + 1;
		}
	}
	EmitField(Len);

	if (Kept.Num() == 0)
	{
		InOutStructText.Reset();
		return;
	}
	InOutStructText = TEXT("(") + FString::Join(Kept, TEXT(",")) + TEXT(")");
}

void AddGenericNodePropertyText_ExportBpy(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& NodeProps,
	const TCHAR* PropertyName,
	bool bUseDelta = false)
{
	if (!Node || !NodeProps.IsValid() || !PropertyName)
	{
		return;
	}

	FProperty* Property = Node->GetClass()->FindPropertyByName(FName(PropertyName));
	if (!Property)
	{
		return;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
	if (!ValuePtr)
	{
		return;
	}

	void* DefaultPtr = nullptr;
	if (bUseDelta)
	{
		if (UObject* DefaultObject = Node->GetClass()->GetDefaultObject())
		{
			DefaultPtr = Property->ContainerPtrToValuePtr<void>(DefaultObject);
		}
	}

	FString ExportedValue;
	Property->ExportTextItem_Direct(ExportedValue, ValuePtr, DefaultPtr, Node, PPF_None);
	if (!ExportedValue.IsEmpty())
	{
		NodeProps->SetStringField(PropertyName, ExportedValue);
	}
}

static FString ReadNodePropertyAsText_ExportBpy(
	const UEdGraphNode* Node,
	const TCHAR* PropertyName)
{
	if (!Node || !PropertyName || !*PropertyName)
	{
		return FString();
	}

	FProperty* Property = Node->GetClass()->FindPropertyByName(FName(PropertyName));
	if (!Property)
	{
		return FString();
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(const_cast<UEdGraphNode*>(Node));
	if (!ValuePtr)
	{
		return FString();
	}

	FString ExportedValue;
	Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, const_cast<UEdGraphNode*>(Node), PPF_None);
	return ExportedValue;
}

static FString ExtractStateResultHookFunctionName_ExportBpy(
	const FString& StateResultNodeStructText,
	const TCHAR* HookName)
{
	if (StateResultNodeStructText.IsEmpty() || !HookName || !*HookName)
	{
		return FString();
	}

	const FString HookToken = FString::Printf(TEXT("%s=(FunctionName=\""), HookName);
	const int32 HookPos = StateResultNodeStructText.Find(HookToken, ESearchCase::CaseSensitive);
	if (HookPos == INDEX_NONE)
	{
		return FString();
	}

	const int32 NameStart = HookPos + HookToken.Len();
	const int32 NameEnd = StateResultNodeStructText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
	if (NameEnd == INDEX_NONE || NameEnd <= NameStart)
	{
		return FString();
	}

	return StateResultNodeStructText.Mid(NameStart, NameEnd - NameStart);
}

static FString BuildStateEnteredText_ExportBpy(
	const FString& DesiredNotifyName,
	const FGuid& DesiredNotifyGuid)
{
	if (DesiredNotifyName.IsEmpty())
	{
		return FString(TEXT("(NotifyName=\"None\")"));
	}
	return DesiredNotifyGuid.IsValid()
		? FString::Printf(TEXT("(NotifyName=\"%s\",Guid=%s)"), *DesiredNotifyName, *DesiredNotifyGuid.ToString(EGuidFormats::Digits))
		: FString::Printf(TEXT("(NotifyName=\"%s\")"), *DesiredNotifyName);
}


static FString ExtractMemberReferenceNameFromText_ExportBpy(const FString& MemberReferenceText)
{
	const FString Needle = TEXT("MemberName=\"");
	const int32 Pos = MemberReferenceText.Find(Needle, ESearchCase::CaseSensitive);
	if (Pos == INDEX_NONE)
	{
		return FString();
	}
	const int32 ValueStart = Pos + Needle.Len();
	const int32 ValueEnd = MemberReferenceText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
	if (ValueEnd == INDEX_NONE || ValueEnd <= ValueStart)
	{
		return FString();
	}
	const FString Name = MemberReferenceText.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();
	return Name.Equals(TEXT("None"), ESearchCase::IgnoreCase) ? FString() : Name;
}

static void NormalizeStateResultRuntimeNodeTextFromEditorRefs_ExportBpy(FNodeInfo& Info)
{
	FString* NodeText = Info.NodeProps.Find(TEXT("Node"));
	const FString* EditorStateEntryText = Info.NodeProps.Find(TEXT("StateEntryFunction"));
	if (!NodeText || !EditorStateEntryText)
	{
		return;
	}

	const FString EditorStateEntryName = ExtractMemberReferenceNameFromText_ExportBpy(*EditorStateEntryText);
	if (EditorStateEntryName.IsEmpty())
	{
		return;
	}

	const FString FunctionNeedle = TEXT("StateEntryFunction=(FunctionName=\"");
	const int32 FunctionPos = NodeText->Find(FunctionNeedle, ESearchCase::CaseSensitive);
	if (FunctionPos == INDEX_NONE)
	{
		return;
	}

	const int32 ValueStart = FunctionPos + FunctionNeedle.Len();
	const int32 ValueEnd = NodeText->Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
	if (ValueEnd == INDEX_NONE || ValueEnd < ValueStart)
	{
		return;
	}

	NodeText->RemoveAt(ValueStart, ValueEnd - ValueStart, EAllowShrinking::No);
	NodeText->InsertAt(ValueStart, EditorStateEntryName);
}

static FString ExtractStateNotifyNameFromStateEnteredText_ExportBpy(const FString& StateEnteredText)
{
	const FString Token = TEXT("NotifyName=\"");
	const int32 Pos = StateEnteredText.Find(Token, ESearchCase::CaseSensitive);
	if (Pos == INDEX_NONE)
	{
		return FString();
	}

	const int32 NameStart = Pos + Token.Len();
	const int32 NameEnd = StateEnteredText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
	if (NameEnd == INDEX_NONE || NameEnd <= NameStart)
	{
		return FString();
	}
	return StateEnteredText.Mid(NameStart, NameEnd - NameStart);
}

FString GetReferencedNodeSerializedUid_ExportBpy(const UEdGraphNode* ReferencedNode)
{
	if (!ReferencedNode || !ReferencedNode->NodeGuid.IsValid())
	{
		return FString();
	}

	return ReferencedNode->NodeGuid.ToString(EGuidFormats::Digits);
}

bool IsSupportedNonK2GraphNode_ExportBpy(const UEdGraphNode* Node)
{
	return Node &&
		(Node->IsA<UAnimStateEntryNode>() ||
		Node->IsA<UAnimStateNode>() ||
		Node->IsA<UAnimStateTransitionNode>() ||
		Node->IsA<UAnimStateAliasNode>() ||
		Node->IsA<UAnimStateConduitNode>() ||
		Node->IsA<UEdGraphNode_Comment>());
}

FString SummarizeNodeClasses_ExportBpy(const TArray<UEdGraphNode*>& Nodes, const int32 MaxClasses = 12)
{
	if (Nodes.Num() == 0)
	{
		return TEXT("none");
	}

	TMap<FString, int32> ClassCounts;
	for (const UEdGraphNode* Node : Nodes)
	{
		const FString ClassName = (Node && Node->GetClass())
			? Node->GetClass()->GetName()
			: TEXT("Unknown");
		ClassCounts.FindOrAdd(ClassName)++;
	}

	TArray<FString> ClassKeys;
	ClassCounts.GetKeys(ClassKeys);
	ClassKeys.Sort();

	TArray<FString> Parts;
	int32 Emitted = 0;
	for (const FString& Key : ClassKeys)
	{
		if (Emitted >= MaxClasses)
		{
			const int32 Remaining = ClassKeys.Num() - Emitted;
			if (Remaining > 0)
			{
				Parts.Add(FString::Printf(TEXT("...+%d classes"), Remaining));
			}
			break;
		}

		Parts.Add(FString::Printf(TEXT("%s=%d"), *Key, ClassCounts[Key]));
		++Emitted;
	}

	return FString::Join(Parts, TEXT(", "));
}

bool IsBlendStackGraphLike_ExportBpy(const UEdGraph* Graph)
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

FString GetGraphOuterKind_ExportBpy(const UEdGraph* Graph)
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

UEdGraph* ResolveBlendStackGraph_ExportBpy(const UK2Node* Node)
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
			if (IsBlendStackGraphLike_ExportBpy(Graph) &&
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
		if (!bPropertyLooksRelevant && !IsBlendStackGraphLike_ExportBpy(Graph))
		{
			continue;
		}

		int32 Score = Graph->Nodes.Num();
		if (PropertyName.Contains(TEXT("BlendStack"), ESearchCase::IgnoreCase))
		{
			Score += 500;
		}
		if (IsBlendStackGraphLike_ExportBpy(Graph))
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

UBlueprint* LoadBlueprintAsset_ExportBpy(const FString& BlueprintPath, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!BP)
	{
		BP = Cast<UBlueprint>(
			StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, *BlueprintPath));
	}

	if (!BP)
	{
		OutError = FString::Printf(TEXT("Cannot load blueprint: %s"), *BlueprintPath);
	}

	return BP;
}

FString SerializeJsonPretty_ExportBpy(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return TEXT("{}");
	}

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	return Output;
}

FString SerializeJsonCompact_ExportBpy(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return TEXT("{}");
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	return Output;
}

bool ParseJsonObject_ExportBpy(const FString& JsonText, TSharedPtr<FJsonObject>& OutJsonObject)
{
	OutJsonObject.Reset();
	if (JsonText.IsEmpty())
	{
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	return FJsonSerializer::Deserialize(Reader, OutJsonObject) && OutJsonObject.IsValid();
}

FString JsonValueToPythonLiteral_ExportBpy(const TSharedPtr<FJsonValue>& JsonValue, int32 IndentLevel);

FString JsonObjectToPythonLiteral_ExportBpy(const TSharedPtr<FJsonObject>& JsonObject, int32 IndentLevel)
{
	if (!JsonObject.IsValid())
	{
		return TEXT("{}");
	}

	TArray<FString> Keys;
	JsonObject->Values.GetKeys(Keys);
	Keys.Sort();
	if (Keys.IsEmpty())
	{
		return TEXT("{}");
	}

	const FString Indent = FString::ChrN(IndentLevel, TEXT(' '));
	const FString ChildIndent = FString::ChrN(IndentLevel + 4, TEXT(' '));

	FString Result = TEXT("{\n");
	for (const FString& Key : Keys)
	{
		const TSharedPtr<FJsonValue>* JsonValue = JsonObject->Values.Find(Key);
		if (!JsonValue || !JsonValue->IsValid())
		{
			continue;
		}

		Result += ChildIndent +
			MakePythonStringLiteral_ExportBpy(Key) +
			TEXT(": ") +
			JsonValueToPythonLiteral_ExportBpy(*JsonValue, IndentLevel + 4) +
			TEXT(",\n");
	}

	Result += Indent + TEXT("}");
	return Result;
}

FString JsonArrayToPythonLiteral_ExportBpy(const TArray<TSharedPtr<FJsonValue>>& JsonArray, int32 IndentLevel)
{
	if (JsonArray.IsEmpty())
	{
		return TEXT("[]");
	}

	const FString Indent = FString::ChrN(IndentLevel, TEXT(' '));
	const FString ChildIndent = FString::ChrN(IndentLevel + 4, TEXT(' '));

	FString Result = TEXT("[\n");
	for (const TSharedPtr<FJsonValue>& JsonValue : JsonArray)
	{
		Result += ChildIndent + JsonValueToPythonLiteral_ExportBpy(JsonValue, IndentLevel + 4) + TEXT(",\n");
	}

	Result += Indent + TEXT("]");
	return Result;
}

FString JsonValueToPythonLiteral_ExportBpy(const TSharedPtr<FJsonValue>& JsonValue, int32 IndentLevel)
{
	if (!JsonValue.IsValid())
	{
		return TEXT("None");
	}

	switch (JsonValue->Type)
	{
	case EJson::String:
		return MakePythonStringLiteral_ExportBpy(JsonValue->AsString());
	case EJson::Number:
		return FString::SanitizeFloat(JsonValue->AsNumber());
	case EJson::Boolean:
		return JsonValue->AsBool() ? TEXT("True") : TEXT("False");
	case EJson::Array:
		return JsonArrayToPythonLiteral_ExportBpy(JsonValue->AsArray(), IndentLevel);
	case EJson::Object:
		return JsonObjectToPythonLiteral_ExportBpy(JsonValue->AsObject(), IndentLevel);
	case EJson::Null:
	default:
		return TEXT("None");
	}
}

FString MakePythonMultilineStringLiteral_ExportBpy(const FString& Text)
{
	if (!Text.Contains(TEXT("'''")))
	{
		return FString::Printf(TEXT("r'''%s'''"), *Text);
	}

	if (!Text.Contains(TEXT("\"\"\"")))
	{
		return FString::Printf(TEXT("r\"\"\"%s\"\"\""), *Text);
	}

	FString Escaped = Text;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("'''"), TEXT("\\'\\'\\'"));
	return FString::Printf(TEXT("'''%s'''"), *Escaped);
}

FString GetJsonStringField_ExportBpy(
	const TSharedPtr<FJsonObject>& JsonObject,
	const TCHAR* FieldName,
	const FString& Fallback = FString())
{
	if (JsonObject.IsValid() && JsonObject->HasTypedField<EJson::String>(FieldName))
	{
		return JsonObject->GetStringField(FieldName);
	}

	return Fallback;
}

FString GetSCSNodeName_ExportBpy(const USCS_Node* Node)
{
	if (!Node)
	{
		return FString();
	}

	FString NodeName = Node->GetVariableName().ToString();
	if (NodeName.IsEmpty() || NodeName == TEXT("None"))
	{
		NodeName = Node->ComponentTemplate ? Node->ComponentTemplate->GetName() : FString();
	}

	return NodeName;
}

FString SanitizeExportedComponentName_ExportBpy(FString Name)
{
	Name.TrimStartAndEndInline();
	if (Name.EndsWith(TEXT("_GEN_VARIABLE")))
	{
		Name.LeftChopInline(FCString::Strlen(TEXT("_GEN_VARIABLE")));
	}
	if (Name == TEXT("None"))
	{
		Name.Reset();
	}
	return Name;
}

FString ResolveParentComponentTemplateName_ExportBpy(
	const UBlueprint* Blueprint,
	const USCS_Node* Node,
	const USceneComponent* ParentTemplate)
{
	if (!ParentTemplate)
	{
		return FString();
	}

	if (Blueprint && Blueprint->SimpleConstructionScript)
	{
		UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		for (const USCS_Node* OtherNode : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!OtherNode || OtherNode == Node || !OtherNode->ComponentTemplate)
			{
				continue;
			}

			const UActorComponent* CandidateTemplate = OtherNode->ComponentTemplate;
			if (CandidateTemplate == ParentTemplate ||
				CandidateTemplate->GetFName() == ParentTemplate->GetFName())
			{
				return SanitizeExportedComponentName_ExportBpy(GetSCSNodeName_ExportBpy(OtherNode));
			}

			if (GeneratedClass)
			{
				const UActorComponent* ActualTemplate = OtherNode->GetActualComponentTemplate(GeneratedClass);
				if (ActualTemplate &&
					(ActualTemplate == ParentTemplate || ActualTemplate->GetFName() == ParentTemplate->GetFName()))
				{
					return SanitizeExportedComponentName_ExportBpy(GetSCSNodeName_ExportBpy(OtherNode));
				}
			}
		}
	}

	return SanitizeExportedComponentName_ExportBpy(ParentTemplate->GetFName().ToString());
}

FString ResolveTemplateAttachParentName_ExportBpy(const UBlueprint* Blueprint, const USCS_Node* Node)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript || !Node)
	{
		return FString();
	}

	if (const USceneComponent* SceneTemplate = Cast<USceneComponent>(Node->ComponentTemplate))
	{
		if (const USceneComponent* AttachParent = SceneTemplate->GetAttachParent())
		{
			return ResolveParentComponentTemplateName_ExportBpy(Blueprint, Node, AttachParent);
		}
	}

	if (UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
	{
		if (const USceneComponent* ActualSceneTemplate = Cast<USceneComponent>(Node->GetActualComponentTemplate(GeneratedClass)))
		{
			if (const USceneComponent* AttachParent = ActualSceneTemplate->GetAttachParent())
			{
				return ResolveParentComponentTemplateName_ExportBpy(Blueprint, Node, AttachParent);
			}
		}
	}

	if (const USceneComponent* ParentTemplate = Node->GetParentComponentTemplate(const_cast<UBlueprint*>(Blueprint)))
	{
		return ResolveParentComponentTemplateName_ExportBpy(Blueprint, Node, ParentTemplate);
	}

	return FString();
}

FString ResolveComponentAttachToName_ExportBpy(const UBlueprint* Blueprint, const USCS_Node* Node)
{
	if (!Node)
	{
		return FString();
	}

	if (Node->AttachToName != NAME_None)
	{
		return Node->AttachToName.ToString();
	}

	auto ResolveSceneAttachSocketName = [](const USceneComponent* SceneTemplate) -> FString
	{
		if (!SceneTemplate)
		{
			return FString();
		}

		const FName AttachSocketName = SceneTemplate->GetAttachSocketName();
		return AttachSocketName != NAME_None ? AttachSocketName.ToString() : FString();
	};

	const FString TemplateAttachSocketName = ResolveSceneAttachSocketName(Cast<USceneComponent>(Node->ComponentTemplate));
	if (!TemplateAttachSocketName.IsEmpty())
	{
		return TemplateAttachSocketName;
	}

	if (Blueprint)
	{
		if (UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
		{
			const FString ActualAttachSocketName = ResolveSceneAttachSocketName(
				Cast<USceneComponent>(Node->GetActualComponentTemplate(GeneratedClass)));
			if (!ActualAttachSocketName.IsEmpty())
			{
				return ActualAttachSocketName;
			}
		}
	}

	return FString();
}

bool FindParentSCSNodeRecursive_ExportBpy(
	const USCS_Node* SearchNode,
	const USCS_Node* TargetNode,
	const USCS_Node*& OutParentNode)
{
	if (!SearchNode || !TargetNode)
	{
		return false;
	}

	for (const USCS_Node* ChildNode : SearchNode->GetChildNodes())
	{
		if (!ChildNode)
		{
			continue;
		}

		if (ChildNode == TargetNode)
		{
			OutParentNode = SearchNode;
			return true;
		}

		if (FindParentSCSNodeRecursive_ExportBpy(ChildNode, TargetNode, OutParentNode))
		{
			return true;
		}
	}

	return false;
}

FString ResolveComponentParentName_ExportBpy(const UBlueprint* Blueprint, const USCS_Node* Node);

TArray<USCS_Node*> GetSCSNodesParentFirst_ExportBpy(const UBlueprint* Blueprint)
{
	TArray<USCS_Node*> OrderedNodes;
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return OrderedNodes;
	}

	const TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
	TMap<FString, USCS_Node*> NodeByName;
	NodeByName.Reserve(AllNodes.Num());
	for (USCS_Node* Node : AllNodes)
	{
		if (!Node)
		{
			continue;
		}

		const FString NodeName = SanitizeExportedComponentName_ExportBpy(GetSCSNodeName_ExportBpy(Node));
		if (!NodeName.IsEmpty() && !NodeByName.Contains(NodeName))
		{
			NodeByName.Add(NodeName, Node);
		}
	}

	TSet<const USCS_Node*> VisitingNodes;
	TSet<const USCS_Node*> VisitedNodes;
	TFunction<void(USCS_Node*)> VisitNode = [&](USCS_Node* Node)
	{
		if (!Node || VisitedNodes.Contains(Node))
		{
			return;
		}

		// Cycle guard.
		if (VisitingNodes.Contains(Node))
		{
			return;
		}

		VisitingNodes.Add(Node);

		const FString ParentName = ResolveComponentParentName_ExportBpy(Blueprint, Node);
		if (!ParentName.IsEmpty())
		{
			if (USCS_Node** ParentNodePtr = NodeByName.Find(ParentName))
			{
				VisitNode(*ParentNodePtr);
			}
		}

		VisitingNodes.Remove(Node);
		VisitedNodes.Add(Node);
		OrderedNodes.Add(Node);
	};

	for (USCS_Node* Node : AllNodes)
	{
		VisitNode(Node);
	}

	return OrderedNodes;
}

FString ResolveComponentParentName_ExportBpy(const UBlueprint* Blueprint, const USCS_Node* Node)
{
	if (!Node)
	{
		return FString();
	}

	const FString TemplateParentName = ResolveTemplateAttachParentName_ExportBpy(Blueprint, Node);
	if (!TemplateParentName.IsEmpty())
	{
		return TemplateParentName;
	}

	if (Node->ParentComponentOrVariableName != NAME_None)
	{
		return SanitizeExportedComponentName_ExportBpy(Node->ParentComponentOrVariableName.ToString());
	}

	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return FString();
	}

	const USCS_Node* ParentNode = nullptr;
	for (const USCS_Node* RootNode : Blueprint->SimpleConstructionScript->GetRootNodes())
	{
		if (!RootNode)
		{
			continue;
		}

		if (RootNode == Node)
		{
			return FString();
		}

		if (FindParentSCSNodeRecursive_ExportBpy(RootNode, Node, ParentNode))
		{
			break;
		}
	}

	const FString ParentName = SanitizeExportedComponentName_ExportBpy(GetSCSNodeName_ExportBpy(ParentNode));
	if (!ParentName.IsEmpty())
	{
		return ParentName;
	}
	return FString();
}

FString BuildDefaultBpyExportPath_ExportBpy(UBlueprint* Blueprint)
{
	const FString BlueprintName = FPaths::MakeValidFileName(Blueprint ? Blueprint->GetName() : TEXT("Unknown"));
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedBlueprints"), TEXT("bpy"), BlueprintName, BlueprintName + TEXT(".bp.py"));
}

FString NormalizeStandaloneAssetObjectPath_ExportBpy(const FString& AssetPath)
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

bool ShouldSkipStandaloneProperty_ExportBpy(const FProperty* Property)
{
	return !Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_Deprecated);
}

FString MakeInstancedObjectReferenceLiteral_ExportBpy(const UObject* Object)
{
	if (!Object)
	{
		return TEXT("");
	}

	return FString::Printf(
		TEXT("\"%s'%s'\""),
		*Object->GetClass()->GetPathName(),
		*Object->GetPathName());
}

template <typename TObjectType>
FString SerializeInstancedObjectArray_ExportBpy(const TArray<TObjectPtr<TObjectType>>& Objects)
{
	FString Result = TEXT("(");
	bool bFirst = true;

	for (TObjectType* Object : Objects)
	{
		if (!Object)
		{
			continue;
		}

		if (!bFirst)
		{
			Result += TEXT(",");
		}
		bFirst = false;
		Result += MakeInstancedObjectReferenceLiteral_ExportBpy(Object);
	}

	Result += TEXT(")");
	return Result;
}

void AppendInputActionStandaloneProperties_ExportBpy(
	UObject* Asset,
	const TSharedPtr<FJsonObject>& PropertiesJson)
{
	UInputAction* const InputAction = Cast<UInputAction>(Asset);
	if (!InputAction || !PropertiesJson.IsValid())
	{
		return;
	}

	if (InputAction->Triggers.Num() > 0)
	{
		PropertiesJson->SetStringField(
			TEXT("Triggers"),
			SerializeInstancedObjectArray_ExportBpy(InputAction->Triggers));
	}

	if (InputAction->Modifiers.Num() > 0)
	{
		PropertiesJson->SetStringField(
			TEXT("Modifiers"),
			SerializeInstancedObjectArray_ExportBpy(InputAction->Modifiers));
	}
}

TSharedPtr<FJsonObject> SerializeObjectProperties_ExportBpy(UObject* Object, const UObject* DefaultsObject)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Object)
	{
		return Result;
	}

	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		if (ShouldSkipStandaloneProperty_ExportBpy(Property))
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		const void* DefaultPtr = DefaultsObject ? Property->ContainerPtrToValuePtr<void>(DefaultsObject) : nullptr;
		if (DefaultPtr && Property->Identical(ValuePtr, DefaultPtr, PPF_None))
		{
			continue;
		}

		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, ValuePtr, DefaultPtr, Object, PPF_None);
		Result->SetStringField(Property->GetName(), ExportedValue);
	}

	return Result;
}

TArray<TSharedPtr<FJsonValue>> SerializeStandaloneSubobjects_ExportBpy(UObject* Asset)
{
	TArray<TSharedPtr<FJsonValue>> Results;
	if (!Asset)
	{
		return Results;
	}

	TArray<UObject*> Subobjects;
	GetObjectsWithOuter(Asset, Subobjects, /*bIncludeNestedObjects=*/ true);
	Subobjects.RemoveAll([](const UObject* Object)
	{
		return Object == nullptr || Object->HasAnyFlags(RF_Transient | RF_ClassDefaultObject);
	});
	Subobjects.Sort([](const UObject& A, const UObject& B)
	{
		return A.GetPathName() < B.GetPathName();
	});

	for (UObject* Subobject : Subobjects)
	{
		TSharedPtr<FJsonObject> SubobjectJson = MakeShared<FJsonObject>();
		SubobjectJson->SetStringField(TEXT("name"), Subobject->GetName());
		SubobjectJson->SetStringField(TEXT("gate"), Subobject->GetPathName());
		SubobjectJson->SetStringField(TEXT("class"), Subobject->GetClass()->GetPathName());
		SubobjectJson->SetObjectField(
			TEXT("properties"),
			SerializeObjectProperties_ExportBpy(Subobject, Subobject->GetClass()->GetDefaultObject(false)));
		Results.Add(MakeShared<FJsonValueObject>(SubobjectJson));
	}

	return Results;
}

TArray<TSharedPtr<FJsonValue>> SerializeUserDefinedEnumEntries_ExportBpy(const UUserDefinedEnum* UserDefinedEnum)
{
	TArray<TSharedPtr<FJsonValue>> Results;
	if (!UserDefinedEnum)
	{
		return Results;
	}

	for (int32 Index = 0; Index < UserDefinedEnum->NumEnums(); ++Index)
	{
		const FString EnumName = UserDefinedEnum->GetNameStringByIndex(Index);
		if (EnumName.EndsWith(TEXT("_MAX"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		if (UserDefinedEnum->HasMetaData(TEXT("Hidden"), Index))
		{
			continue;
		}

		TSharedPtr<FJsonObject> EntryJson = MakeShared<FJsonObject>();
		EntryJson->SetStringField(TEXT("name"), EnumName);
		EntryJson->SetNumberField(TEXT("value"), static_cast<double>(UserDefinedEnum->GetValueByIndex(Index)));
		EntryJson->SetStringField(TEXT("display_name"), UserDefinedEnum->GetDisplayNameTextByIndex(Index).ToString());
		Results.Add(MakeShared<FJsonValueObject>(EntryJson));
	}

	return Results;
}

bool IsChooserTableAsset_ExportBpy(const UObject* Asset)
{
	return Asset &&
		Asset->GetClass() &&
		Asset->GetClass()->GetPathName().Equals(TEXT("/Script/Chooser.ChooserTable"), ESearchCase::CaseSensitive);
}

FString ExtractChooserAssetReferenceFromInstancedStruct_ExportBpy(const FInstancedStruct& StructValue)
{
	if (!StructValue.IsValid())
	{
		return FString();
	}

	const UScriptStruct* ScriptStruct = StructValue.GetScriptStruct();
	const uint8* StructMemory = StructValue.GetMemory();
	if (!ScriptStruct || !StructMemory)
	{
		return FString();
	}

	const FProperty* AssetProperty = ScriptStruct->FindPropertyByName(TEXT("Asset"));
	if (!AssetProperty)
	{
		return FString();
	}

	const void* ValuePtr = AssetProperty->ContainerPtrToValuePtr<void>(StructMemory);
	if (!ValuePtr)
	{
		return FString();
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(AssetProperty))
	{
		if (const UObject* ReferencedObject = ObjectProperty->GetObjectPropertyValue(ValuePtr))
		{
			return ReferencedObject->GetPathName();
		}
		return FString();
	}

	if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(AssetProperty))
	{
		if (const FSoftObjectPtr* SoftObjectValue = SoftObjectProperty->ContainerPtrToValuePtr<FSoftObjectPtr>(StructMemory))
		{
			const FSoftObjectPath SoftPath = SoftObjectValue->ToSoftObjectPath();
			if (SoftPath.IsValid())
			{
				return SoftPath.ToString();
			}
		}
	}

	return FString();
}

bool ExportChooserPropertyText_ExportBpy(UObject* Asset, const TCHAR* PropertyName, FString& OutText)
{
	OutText.Reset();
	if (!Asset || !PropertyName)
	{
		return false;
	}

	const FProperty* Property = FindFProperty<FProperty>(Asset->GetClass(), PropertyName);
	if (!Property)
	{
		return false;
	}

	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Asset);
	if (!ValuePtr)
	{
		return false;
	}

	Property->ExportTextItem_Direct(OutText, ValuePtr, nullptr, Asset, PPF_None);
	return true;
}

void AppendChooserStandaloneMeta_ExportBpy(UObject* Asset, const TSharedPtr<FJsonObject>& Meta)
{
	if (!Meta.IsValid() || !IsChooserTableAsset_ExportBpy(Asset))
	{
		return;
	}

	if (const FProperty* ResultTypeProperty = FindFProperty<FProperty>(Asset->GetClass(), TEXT("ResultType")))
	{
		const void* ValuePtr = ResultTypeProperty->ContainerPtrToValuePtr<void>(Asset);
		FString ResultTypeText;
		ResultTypeProperty->ExportTextItem_Direct(ResultTypeText, ValuePtr, nullptr, Asset, PPF_None);
		Meta->SetStringField(TEXT("chooser_result_type"), ResultTypeText);
	}

	if (const FObjectPropertyBase* OutputObjectTypeProperty = FindFProperty<FObjectPropertyBase>(Asset->GetClass(), TEXT("OutputObjectType")))
	{
		const void* ValuePtr = OutputObjectTypeProperty->ContainerPtrToValuePtr<void>(Asset);
		if (const UClass* OutputClass = Cast<UClass>(OutputObjectTypeProperty->GetObjectPropertyValue(ValuePtr)))
		{
			Meta->SetStringField(TEXT("chooser_output_object_type"), OutputClass->GetPathName());
		}
		else
		{
			Meta->SetStringField(TEXT("chooser_output_object_type"), TEXT(""));
		}
	}

	if (const FStructProperty* FallbackProperty = FindFProperty<FStructProperty>(Asset->GetClass(), TEXT("FallbackResult")))
	{
		if (FallbackProperty->Struct == FInstancedStruct::StaticStruct())
		{
			if (const FInstancedStruct* FallbackStruct = FallbackProperty->ContainerPtrToValuePtr<FInstancedStruct>(Asset))
			{
				Meta->SetStringField(
					TEXT("chooser_fallback_asset"),
					ExtractChooserAssetReferenceFromInstancedStruct_ExportBpy(*FallbackStruct));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ResultAssetsJson;
	if (const FArrayProperty* ResultsProperty = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("ResultsStructs")))
	{
		const FStructProperty* InnerStructProperty = CastField<FStructProperty>(ResultsProperty->Inner);
		if (InnerStructProperty && InnerStructProperty->Struct == FInstancedStruct::StaticStruct())
		{
			FScriptArrayHelper ResultsArrayHelper(
				ResultsProperty,
				ResultsProperty->ContainerPtrToValuePtr<void>(Asset));

			for (int32 Index = 0; Index < ResultsArrayHelper.Num(); ++Index)
			{
				const FInstancedStruct* ResultStruct =
					reinterpret_cast<const FInstancedStruct*>(ResultsArrayHelper.GetRawPtr(Index));
				if (!ResultStruct)
				{
					continue;
				}

				const FString AssetPath =
					ExtractChooserAssetReferenceFromInstancedStruct_ExportBpy(*ResultStruct);
				if (!AssetPath.IsEmpty())
				{
					ResultAssetsJson.Add(MakeShared<FJsonValueString>(AssetPath));
				}
			}
		}
	}

	Meta->SetArrayField(TEXT("chooser_result_assets"), ResultAssetsJson);

	FString PropertyText;
	if (ExportChooserPropertyText_ExportBpy(Asset, TEXT("FallbackResult"), PropertyText))
	{
		Meta->SetStringField(TEXT("chooser_fallback_result_text"), PropertyText);
	}
	if (ExportChooserPropertyText_ExportBpy(Asset, TEXT("ResultsStructs"), PropertyText))
	{
		Meta->SetStringField(TEXT("chooser_results_structs_text"), PropertyText);
	}
	if (ExportChooserPropertyText_ExportBpy(Asset, TEXT("ColumnsStructs"), PropertyText))
	{
		Meta->SetStringField(TEXT("chooser_columns_structs_text"), PropertyText);
	}
	if (ExportChooserPropertyText_ExportBpy(Asset, TEXT("DisabledRows"), PropertyText))
	{
		Meta->SetStringField(TEXT("chooser_disabled_rows_text"), PropertyText);
	}
	Meta->SetNumberField(TEXT("chooser_structs_export_version"), 1.0);
}

TSharedPtr<FJsonObject> BuildStandaloneAssetMeta_ExportBpy(UObject* Asset)
{
	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
	if (!Asset)
	{
		return Meta;
	}

	Meta->SetStringField(TEXT("kind"), TEXT("standalone_asset"));
	Meta->SetStringField(TEXT("asset"), Asset->GetPathName());
	Meta->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetPathName());
	if (const UUserDefinedEnum* UserDefinedEnum = Cast<UUserDefinedEnum>(Asset))
	{
		Meta->SetStringField(TEXT("export_type"), TEXT("user_defined_enum"));
		Meta->SetArrayField(TEXT("enum_entries"), SerializeUserDefinedEnumEntries_ExportBpy(UserDefinedEnum));
	}
	else
	{
		Meta->SetStringField(TEXT("export_type"), TEXT("generic_object"));
	}
	Meta->SetStringField(TEXT("outer"), Asset->GetOuter() ? Asset->GetOuter()->GetPathName() : TEXT(""));
	Meta->SetStringField(TEXT("package"), Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : TEXT(""));
	TSharedPtr<FJsonObject> PropertiesJson =
		SerializeObjectProperties_ExportBpy(Asset, Asset->GetClass()->GetDefaultObject(false));
	AppendInputActionStandaloneProperties_ExportBpy(Asset, PropertiesJson);
	Meta->SetObjectField(
		TEXT("properties"),
		PropertiesJson);
	Meta->SetArrayField(TEXT("subobjects"), SerializeStandaloneSubobjects_ExportBpy(Asset));
	AppendChooserStandaloneMeta_ExportBpy(Asset, Meta);
	return Meta;
}

FString EscapePythonString_ExportBpy(const FString& Text)
{
	FString Escaped = Text;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Escaped.ReplaceInline(TEXT("\t"), TEXT("\\t"));
	return Escaped;
}

FString MakePythonStringLiteral_ExportBpy(const FString& Text)
{
	return FString::Printf(TEXT("\"%s\""), *EscapePythonString_ExportBpy(Text));
}

FString NormalizeExportedValueText_ExportBpy(FString Text)
{
	Text.ReplaceInline(TEXT("-0.000000"), TEXT("0.000000"), ESearchCase::CaseSensitive);
	return Text;
}

FString GetExportedSubTypeText_ExportBpy(UObject* SubCategoryObject)
{
	if (!SubCategoryObject)
	{
		return FString();
	}

	if (SubCategoryObject->IsA<UUserDefinedEnum>() || SubCategoryObject->IsA<UUserDefinedStruct>())
	{
		return SubCategoryObject->GetPathName();
	}

	return SubCategoryObject->GetName();
}

FString NormalizeTypeCoreString_ExportBpy(
	const FName& Category,
	const FName& SubCategory,
	UObject* SubCategoryObject)
{
	FString TypeStr = Category.ToString();
	if (SubCategoryObject)
	{
		const FString SubTypeText = GetExportedSubTypeText_ExportBpy(SubCategoryObject);
		if (!SubTypeText.IsEmpty())
		{
			TypeStr += SubTypeText.StartsWith(TEXT("/")) ? SubTypeText : TEXT("/") + SubTypeText;
		}
	}
	else if (!SubCategory.IsNone())
	{
		TypeStr += TEXT("/") + SubCategory.ToString();
	}
	return TypeStr;
}

bool HasTerminalTypeData_ExportBpy(const FEdGraphTerminalType& TerminalType)
{
	return !TerminalType.TerminalCategory.IsNone() ||
		!TerminalType.TerminalSubCategory.IsNone() ||
		TerminalType.TerminalSubCategoryObject.IsValid();
}

FString NormalizeTypeString_ExportBpy(const FEdGraphPinType& PinType)
{
	FString TypeStr = NormalizeTypeCoreString_ExportBpy(
		PinType.PinCategory,
		PinType.PinSubCategory,
		PinType.PinSubCategoryObject.Get());
	if (PinType.ContainerType == EPinContainerType::Array)
	{
		TypeStr += TEXT("|array");
	}
	else if (PinType.ContainerType == EPinContainerType::Set)
	{
		TypeStr += TEXT("|set");
	}
	else if (PinType.ContainerType == EPinContainerType::Map)
	{
		TypeStr += TEXT("|map");
	}
	if (PinType.bIsReference)
	{
		TypeStr += TEXT("|ref");
	}
	if (PinType.bIsConst)
	{
		TypeStr += TEXT("|const");
	}
	if (PinType.ContainerType == EPinContainerType::Map && HasTerminalTypeData_ExportBpy(PinType.PinValueType))
	{
		TypeStr += TEXT("|mapvalue=") + NormalizeTypeCoreString_ExportBpy(
			PinType.PinValueType.TerminalCategory,
			PinType.PinValueType.TerminalSubCategory,
			PinType.PinValueType.TerminalSubCategoryObject.Get());
		if (PinType.PinValueType.bTerminalIsConst)
		{
			TypeStr += TEXT("|mapvalueconst");
		}
		if (PinType.PinValueType.bTerminalIsWeakPointer)
		{
			TypeStr += TEXT("|mapvalueweak");
		}
		if (PinType.PinValueType.bTerminalIsUObjectWrapper)
		{
			TypeStr += TEXT("|mapvaluewrapper");
		}
	}
	return TypeStr;
}

FString GetPinContainerString_ExportBpy(const FEdGraphPinType& PinType)
{
	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array:
		return TEXT("array");
	case EPinContainerType::Set:
		return TEXT("set");
	case EPinContainerType::Map:
		return TEXT("map");
	default:
		return TEXT("single");
	}
}

FString GetBlueprintVariableDefaultValue_ExportBpy(UBlueprint* BP, const FBPVariableDescription& Var)
{
	auto SanitizeEmptyContainerAssignments = [](const FString& InValue) -> FString
	{
		if (!InValue.StartsWith(TEXT("(")))
		{
			return InValue;
		}

		FString OutValue;
		OutValue.Reserve(InValue.Len() + 8);

		for (int32 Index = 0; Index < InValue.Len(); ++Index)
		{
			const TCHAR Char = InValue[Index];
			OutValue.AppendChar(Char);

			if (Char == TEXT('=') && (Index + 1) < InValue.Len())
			{
				const TCHAR NextChar = InValue[Index + 1];
				if (NextChar == TEXT(')') || NextChar == TEXT(','))
				{
					OutValue += TEXT("()");
				}
			}
		}

		return OutValue;
	};

	FString DefaultValue = Var.DefaultValue;
	if (!BP)
	{
		return NormalizeExportedValueText_ExportBpy(SanitizeEmptyContainerAssignments(DefaultValue));
	}

	UClass* GeneratedClass = BP->GeneratedClass;
	UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedCDO)
	{
		return NormalizeExportedValueText_ExportBpy(SanitizeEmptyContainerAssignments(DefaultValue));
	}

	const FProperty* TargetProperty = FindFProperty<FProperty>(GeneratedCDO->GetClass(), Var.VarName);
	if (!TargetProperty)
	{
		return NormalizeExportedValueText_ExportBpy(SanitizeEmptyContainerAssignments(DefaultValue));
	}

	FString ExportedValue;
	int32 PortFlags = PPF_None;
	if (TargetProperty->HasAnyPropertyFlags(CPF_ContainsInstancedReference | CPF_InstancedReference))
	{
		PortFlags |= PPF_InstanceSubobjects;
	}

	if (FBlueprintEditorUtils::PropertyValueToString(
		TargetProperty,
		reinterpret_cast<const uint8*>(GeneratedCDO),
		ExportedValue,
		GeneratedCDO,
		PortFlags))
	{
		return NormalizeExportedValueText_ExportBpy(SanitizeEmptyContainerAssignments(ExportedValue));
	}

	return NormalizeExportedValueText_ExportBpy(SanitizeEmptyContainerAssignments(DefaultValue));
}

FString GetPinRawDefaultValue_ExportBpy(const UEdGraphPin* Pin)
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

FString GetPinDefaultValue_ExportBpy(const UEdGraphPin* Pin)
{
	if (!Pin || Pin->Direction != EGPD_Input || !Pin->LinkedTo.IsEmpty())
	{
		return FString();
	}

	return GetPinRawDefaultValue_ExportBpy(Pin);
}

bool ShouldExportDefaultEvenWhenLinked_ExportBpy(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return false;
	}

	const UEdGraphNode* const OwningNode = Pin->GetOwningNode();
	if (!OwningNode)
	{
		return false;
	}

	// Preserve function signature defaults:
	// - FunctionEntry output pins are default input values used by callers.
	if (OwningNode->IsA<UK2Node_FunctionEntry>() && Pin->Direction == EGPD_Output)
	{
		return true;
	}

	return false;
}

FString GetPinDefaultValueForExport_ExportBpy(const UEdGraphPin* Pin)
{
	if (ShouldExportDefaultEvenWhenLinked_ExportBpy(Pin))
	{
		return GetPinRawDefaultValue_ExportBpy(Pin);
	}

	return GetPinDefaultValue_ExportBpy(Pin);
}

bool IsPythonKeyword_ExportBpy(const FString& Name)
{
	static const TSet<FString> Keywords = {
		TEXT("and"), TEXT("as"), TEXT("assert"), TEXT("break"), TEXT("class"),
		TEXT("continue"), TEXT("def"), TEXT("del"), TEXT("elif"), TEXT("else"),
		TEXT("except"), TEXT("finally"), TEXT("for"), TEXT("from"), TEXT("global"),
		TEXT("if"), TEXT("import"), TEXT("in"), TEXT("is"), TEXT("lambda"),
		TEXT("nonlocal"), TEXT("not"), TEXT("or"), TEXT("pass"), TEXT("raise"),
		TEXT("return"), TEXT("try"), TEXT("while"), TEXT("with"), TEXT("yield"),
		TEXT("type"), TEXT("self")
	};
	return Keywords.Contains(Name);
}

FString SanitizePythonIdentifier_ExportBpy(const FString& InName, const FString& Fallback)
{
	FString Safe;
	Safe.Reserve(InName.Len() + 1);

	for (TCHAR Ch : InName)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT('_'))
		{
			Safe.AppendChar(Ch);
		}
		else
		{
			Safe.AppendChar(TEXT('_'));
		}
	}

	while (Safe.Contains(TEXT("__")))
	{
		Safe.ReplaceInline(TEXT("__"), TEXT("_"));
	}

	Safe.TrimStartAndEndInline();
	Safe.TrimCharInline(TEXT('_'), nullptr);
	if (Safe.IsEmpty())
	{
		Safe = Fallback;
	}
	if (!Safe.IsEmpty() && FChar::IsDigit(Safe[0]))
	{
		Safe = TEXT("_") + Safe;
	}
	if (IsPythonKeyword_ExportBpy(Safe))
	{
		Safe += TEXT("_");
	}
	return Safe;
}

bool IsLikelyGuidChunk_ExportBpy(const FString& Chunk)
{
	if (Chunk.Len() < 8)
	{
		return false;
	}

	int32 HexCount = 0;
	for (TCHAR Ch : Chunk)
	{
		if (FChar::IsHexDigit(Ch))
		{
			++HexCount;
			continue;
		}
		if (Ch != TEXT('-'))
		{
			return false;
		}
	}
	return HexCount >= 8;
}

bool IsNumericChunk_ExportBpy(const FString& Chunk)
{
	if (Chunk.IsEmpty())
	{
		return false;
	}

	for (TCHAR Ch : Chunk)
	{
		if (!FChar::IsDigit(Ch))
		{
			return false;
		}
	}

	return true;
}

FString StripGuidSuffix_ExportBpy(const FString& RawName)
{
	FString Result = RawName;
	int32 UnderscoreIndex = INDEX_NONE;
	while (Result.FindLastChar(TEXT('_'), UnderscoreIndex))
	{
		const FString Tail = Result.Mid(UnderscoreIndex + 1);
		if (!IsLikelyGuidChunk_ExportBpy(Tail))
		{
			break;
		}
		Result = Result.Left(UnderscoreIndex);
	}

	while (Result.FindLastChar(TEXT('_'), UnderscoreIndex))
	{
		const FString Tail = Result.Mid(UnderscoreIndex + 1);
		if (!IsNumericChunk_ExportBpy(Tail))
		{
			break;
		}
		Result = Result.Left(UnderscoreIndex);
	}

	return Result;
}

FString GetLogicalPinName_ExportBpy(UK2Node* Node, UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return FString();
	}

	const FString RawPinName = Pin->PinName.ToString();
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		if (Node->IsA<UK2Node_IfThenElse>() && Pin->Direction == EGPD_Output)
		{
			if (RawPinName == UEdGraphSchema_K2::PN_Then.ToString())
			{
				return TEXT("True");
			}
			if (RawPinName == UEdGraphSchema_K2::PN_Else.ToString())
			{
				return TEXT("False");
			}
		}
		return RawPinName;
	}

	if (const UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
	{
		const FString VariableName = VariableGet->VariableReference.GetMemberName().ToString();
		const FString CleanPinName = StripGuidSuffix_ExportBpy(RawPinName);
		if (CleanPinName == VariableName)
		{
			return VariableName;
		}
		if (!VariableName.IsEmpty() && CleanPinName.StartsWith(VariableName + TEXT("_")))
		{
			return CleanPinName.RightChop(VariableName.Len() + 1);
		}
		return CleanPinName;
	}

	if (Node->IsA<UK2Node_BreakStruct>() || Node->IsA<UK2Node_MakeStruct>())
	{
		return StripGuidSuffix_ExportBpy(RawPinName);
	}

	return RawPinName;
}

TArray<UEdGraphPin*> GetSyntheticExecPassThroughTargets_ExportBpy(UK2Node* Node, UEdGraphPin* OutputExecPin)
{
	TArray<UEdGraphPin*> Targets;
	if (!Node || !OutputExecPin)
	{
		return Targets;
	}

	if (OutputExecPin->Direction != EGPD_Output ||
		OutputExecPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec ||
		!OutputExecPin->LinkedTo.IsEmpty())
	{
		return Targets;
	}

	UEdGraphPin* MatchingInputExecPin = nullptr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin == OutputExecPin)
		{
			continue;
		}

		if (Pin->Direction == EGPD_Input &&
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
			Pin->PinName == OutputExecPin->PinName)
		{
			MatchingInputExecPin = Pin;
			break;
		}
	}

	if (!MatchingInputExecPin || MatchingInputExecPin->LinkedTo.Num() < 2)
	{
		return Targets;
	}

	bool bHasUpstreamSource = false;
	for (UEdGraphPin* LinkedPin : MatchingInputExecPin->LinkedTo)
	{
		if (LinkedPin && LinkedPin->GetOwningNode() != Node && LinkedPin->Direction == EGPD_Output)
		{
			bHasUpstreamSource = true;
			break;
		}
	}

	if (!bHasUpstreamSource)
	{
		return Targets;
	}

	for (UEdGraphPin* LinkedPin : MatchingInputExecPin->LinkedTo)
	{
		if (!LinkedPin || LinkedPin->GetOwningNode() == Node)
		{
			continue;
		}

		if (LinkedPin->Direction == EGPD_Input)
		{
			Targets.Add(LinkedPin);
		}
	}

	return Targets;
}

bool CanUseAttributeSyntax_ExportBpy(const FString& PinName)
{
	if (PinName.IsEmpty())
	{
		return false;
	}
	if (IsPythonKeyword_ExportBpy(PinName))
	{
		return PinName == TEXT("self");
	}
	if (!FChar::IsAlpha(PinName[0]) && PinName[0] != TEXT('_'))
	{
		return false;
	}
	for (TCHAR Ch : PinName)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
		{
			return false;
		}
	}
	return true;
}

bool LooksLikeStrictPythonNumberLiteral_ExportBpy(const FString& Value)
{
	if (Value.IsEmpty())
	{
		return false;
	}

	int32 Index = 0;
	if (Value[Index] == TEXT('+') || Value[Index] == TEXT('-'))
	{
		++Index;
		if (Index >= Value.Len())
		{
			return false;
		}
	}

	bool bSawDigit = false;
	bool bSawDot = false;
	bool bSawExponent = false;
	bool bSawExponentDigit = false;

	for (; Index < Value.Len(); ++Index)
	{
		const TCHAR Ch = Value[Index];
		if (FChar::IsDigit(Ch))
		{
			bSawDigit = true;
			if (bSawExponent)
			{
				bSawExponentDigit = true;
			}
			continue;
		}

		if (Ch == TEXT('.') && !bSawDot && !bSawExponent)
		{
			bSawDot = true;
			continue;
		}

		if ((Ch == TEXT('e') || Ch == TEXT('E')) && !bSawExponent && bSawDigit)
		{
			bSawExponent = true;
			bSawExponentDigit = false;
			continue;
		}

		if ((Ch == TEXT('+') || Ch == TEXT('-')) &&
			Index > 0 &&
			(Value[Index - 1] == TEXT('e') || Value[Index - 1] == TEXT('E')))
		{
			continue;
		}

		return false;
	}

	return bSawExponent ? (bSawDigit && bSawExponentDigit) : bSawDigit;
}

FString FormatPythonValueLiteral_ExportBpy(const FString& RawValue)
{
	FString Trimmed = RawValue;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.IsEmpty())
	{
		return MakePythonStringLiteral_ExportBpy(TEXT(""));
	}

	if (Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase))
	{
		return TEXT("True");
	}
	if (Trimmed.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		return TEXT("False");
	}

	for (const TCHAR Ch : Trimmed)
	{
		if (FChar::IsWhitespace(Ch))
		{
			return MakePythonStringLiteral_ExportBpy(Trimmed);
		}
	}

	if (!Trimmed.Contains(TEXT(",")) && LooksLikeStrictPythonNumberLiteral_ExportBpy(Trimmed))
	{
		return Trimmed;
	}

	return MakePythonStringLiteral_ExportBpy(Trimmed);
}

bool IsRealPinTypeString_ExportBpy(const FString& PinType)
{
	// Normalized pin types use "real/float" and "real/double".
	return PinType.StartsWith(TEXT("real/"));
}

FString ForceRealLiteralIfNeeded_ExportBpy(const FString& Literal, const FString& PinType)
{
	if (!IsRealPinTypeString_ExportBpy(PinType))
	{
		return Literal;
	}

	FString Trimmed = Literal;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.IsEmpty())
	{
		return Literal;
	}

	// Preserve non-numeric forms as-is (quoted strings, tuples, structs, etc.).
	if (!LooksLikeStrictPythonNumberLiteral_ExportBpy(Trimmed))
	{
		return Literal;
	}

	// Keep explicit float syntax untouched.
	if (Trimmed.Contains(TEXT(".")) ||
		Trimmed.Contains(TEXT("e")) ||
		Trimmed.Contains(TEXT("E")))
	{
		return Trimmed;
	}

	// Integer-looking value on a real pin must stay real-typed.
	const double NumericValue = FCString::Atod(*Trimmed);
	return FString::Printf(TEXT("%.6f"), NumericValue);
}

TArray<FString> InlineExtraPropLines_ExportBpy(const FNodeInfo& Info)
{
	auto ShouldInlineNodeProp = [&](const FString& Key) -> bool
	{
		if (Key == TEXT("Chooser") ||
			Key == TEXT("Mode") ||
			Key == TEXT("bReturnSoftObjectReference"))
		{
			return true;
		}

		if (!Info.NodeType.StartsWith(TEXT("AnimGraphNode_")))
		{
			return false;
		}

		return Key == TEXT("Node") ||
			Key == TEXT("ShowPinForProperties") ||
			Key == TEXT("CustomPinProperties") ||
			Key == TEXT("BindingPropertyBindings") ||
			Key == TEXT("InitialUpdateFunction") ||
			Key == TEXT("BecomeRelevantFunction") ||
			Key == TEXT("UpdateFunction") ||
			Key == TEXT("OnMotionMatchingStateUpdatedFunction");
	};

	TArray<FString> Keys;
	Info.NodeProps.GetKeys(Keys);
	Keys.Sort();

	TArray<FString> Lines;
	for (const FString& Key : Keys)
	{
		if (!ShouldInlineNodeProp(Key))
		{
			continue;
		}

		const FString* Value = Info.NodeProps.Find(Key);
		if (!Value || Value->IsEmpty())
		{
			continue;
		}

		Lines.Add(FString::Printf(
			TEXT("%s.set_extra_prop(%s, %s)"),
			*Info.VarName,
			*MakePythonStringLiteral_ExportBpy(Key),
			*FormatPythonValueLiteral_ExportBpy(*Value)));
	}

	return Lines;
}

bool ShouldExternalizeNodeProp_ExportBpy(const FString& Key)
{
	return Key == TEXT("BoundGraphJson") ||
		Key == TEXT("BlendStackGraphJson") ||
		Key == TEXT("StateMachineGraphJson") ||
		Key == TEXT("CustomTransitionGraphJson");
}

FString BuildNestedGraphModuleStem_ExportBpy(
	const FString& ParentModuleName,
	const FString& OwnerNodeClass,
	const FString& GraphName)
{
	const FString ParentStem = ParentModuleName.StartsWith(TEXT("other_"))
		? ParentModuleName
		: (TEXT("other_") + ParentModuleName);

	FString NodeCategory = OwnerNodeClass;
	if (NodeCategory == TEXT("AnimStateNode"))
	{
		NodeCategory = TEXT("State");
	}
	else if (NodeCategory == TEXT("AnimStateTransitionNode"))
	{
		NodeCategory = TEXT("Transition");
	}
	else if (NodeCategory == TEXT("AnimStateConduitNode"))
	{
		NodeCategory = TEXT("Conduit");
	}
	else if (NodeCategory == TEXT("AnimStateEntryNode"))
	{
		NodeCategory = TEXT("Entry");
	}
	else
	{
		static const TCHAR* Prefixes[] = {
			TEXT("AnimGraphNode_"),
			TEXT("AnimState"),
			TEXT("K2Node_"),
			TEXT("EdGraphNode_")
		};
		for (const TCHAR* Prefix : Prefixes)
		{
			if (NodeCategory.StartsWith(Prefix))
			{
				NodeCategory.RightChopInline(FCString::Strlen(Prefix), EAllowShrinking::No);
				break;
			}
		}

		if (NodeCategory.EndsWith(TEXT("Node")))
		{
			NodeCategory.LeftChopInline(4, EAllowShrinking::No);
		}
	}
	NodeCategory = SanitizePythonIdentifier_ExportBpy(NodeCategory, TEXT("node"));

	const FString GraphToken = SanitizePythonIdentifier_ExportBpy(GraphName, TEXT("graph"));
	FString Result = ParentStem;
	if (!GraphToken.Equals(NodeCategory, ESearchCase::IgnoreCase) &&
		!GraphToken.StartsWith(NodeCategory + TEXT("_"), ESearchCase::IgnoreCase))
	{
		Result += TEXT("__") + NodeCategory;
	}
	Result += TEXT("__") + GraphToken;
	return Result;
}

FString MakeUniqueNestedGraphModuleStem_ExportBpy(
	const FString& DesiredStem,
	TMap<FString, int32>& InOutStemCounts)
{
	int32& Count = InOutStemCounts.FindOrAdd(DesiredStem);
	++Count;
	if (Count <= 1)
	{
		return DesiredStem;
	}

	return FString::Printf(TEXT("%s_%d"), *DesiredStem, Count);
}

bool WriteNestedGraphModule_ExportBpy(
	const FString& OutDir,
	const FString& ModuleStem,
	TMap<FString, int32>& InOutStemCounts,
	const TSharedPtr<FJsonObject>& GraphJson,
	FString& OutError);

FString BuildNestedGraphModuleStemForJsonNode_ExportBpy(
	const FString& ParentModuleName,
	const FString& NodeClassName,
	const FString& GraphName)
{
	return BuildNestedGraphModuleStem_ExportBpy(ParentModuleName, NodeClassName, GraphName);
}

bool CloneJsonObject_ExportBpy(
	const TSharedPtr<FJsonObject>& Source,
	TSharedPtr<FJsonObject>& OutClone)
{
	OutClone.Reset();
	if (!Source.IsValid())
	{
		return false;
	}

	return ParseJsonObject_ExportBpy(SerializeJsonCompact_ExportBpy(Source), OutClone) && OutClone.IsValid();
}

bool JsonObjectToStringMap_ExportBpy(
	const TSharedPtr<FJsonObject>& JsonObject,
	TMap<FString, FString>& OutMap)
{
	OutMap.Reset();
	if (!JsonObject.IsValid())
	{
		return false;
	}

	TArray<FString> Keys;
	JsonObject->Values.GetKeys(Keys);
	for (const FString& Key : Keys)
	{
		const TSharedPtr<FJsonValue>* JsonValue = JsonObject->Values.Find(Key);
		if (!JsonValue || !JsonValue->IsValid())
		{
			continue;
		}

		FString StringValue;
		switch ((*JsonValue)->Type)
		{
		case EJson::String:
			StringValue = (*JsonValue)->AsString();
			break;
		case EJson::Number:
			StringValue = FString::SanitizeFloat((*JsonValue)->AsNumber());
			break;
		case EJson::Boolean:
			StringValue = (*JsonValue)->AsBool() ? TEXT("True") : TEXT("False");
			break;
		case EJson::Object:
			StringValue = SerializeJsonCompact_ExportBpy((*JsonValue)->AsObject());
			break;
		case EJson::Array:
		{
			TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
			Wrapper->SetArrayField(TEXT("Value"), (*JsonValue)->AsArray());
			StringValue = SerializeJsonCompact_ExportBpy(Wrapper);
			StringValue.RemoveFromStart(TEXT("{\"Value\":"));
			StringValue.RemoveFromEnd(TEXT("}"));
			break;
		}
		default:
			break;
		}

		OutMap.Add(Key, StringValue);
	}

	return OutMap.Num() > 0;
}

FString NormalizeFunctionOwnerClassName_ExportBpy(const FString& RawOwner)
{
	if (RawOwner.IsEmpty())
	{
		return FString();
	}

	FString Owner = RawOwner;
	int32 ScopeIndex = INDEX_NONE;
	if (Owner.FindLastChar(TEXT('.'), ScopeIndex))
	{
		Owner = Owner.Mid(ScopeIndex + 1);
	}
	return Owner;
}

void ParseSerializedFunctionRef_ExportBpy(
	const FString& FunctionRef,
	FString& OutFunctionName,
	FString& OutOwnerClassName)
{
	OutFunctionName = FunctionRef;
	OutOwnerClassName.Reset();

	FString OwnerPath;
	if (FunctionRef.Split(TEXT("::"), &OwnerPath, &OutFunctionName))
	{
		OutOwnerClassName = NormalizeFunctionOwnerClassName_ExportBpy(OwnerPath);
	}
}

bool BuildNodeInfoFromSerializedJson_ExportBpy(
	const TSharedPtr<FJsonObject>& NodeJson,
	FNodeInfo& OutInfo,
	FString& OutNodeUid)
{
	OutInfo = FNodeInfo{};
	OutNodeUid.Reset();
	if (!NodeJson.IsValid())
	{
		return false;
	}

	OutNodeUid = GetJsonStringField_ExportBpy(NodeJson, TEXT("uid"));
	OutInfo.NodeType = GetJsonStringField_ExportBpy(NodeJson, TEXT("node_class"));
	OutInfo.FunctionName = GetJsonStringField_ExportBpy(NodeJson, TEXT("member_name"));
	OutInfo.TargetType = GetJsonStringField_ExportBpy(NodeJson, TEXT("target_type"));
	OutInfo.NodeGuid = GetJsonStringField_ExportBpy(NodeJson, TEXT("node_guid"), OutNodeUid);
	OutInfo.Position.X = NodeJson->HasField(TEXT("pos_x")) ? NodeJson->GetNumberField(TEXT("pos_x")) : 0.0;
	OutInfo.Position.Y = NodeJson->HasField(TEXT("pos_y")) ? NodeJson->GetNumberField(TEXT("pos_y")) : 0.0;

	const FString FunctionRef = GetJsonStringField_ExportBpy(NodeJson, TEXT("function_ref"));
	if (OutInfo.NodeType == TEXT("K2Node_Message"))
	{
		ParseSerializedFunctionRef_ExportBpy(FunctionRef, OutInfo.FunctionName, OutInfo.ClassName);
	}
	else if (!FunctionRef.IsEmpty())
	{
		OutInfo.bIsCallFunctionLike = true;
		ParseSerializedFunctionRef_ExportBpy(FunctionRef, OutInfo.FunctionName, OutInfo.ClassName);
	}

	const TArray<TSharedPtr<FJsonValue>>* CustomParamsArray = nullptr;
	if (NodeJson->TryGetArrayField(TEXT("custom_params"), CustomParamsArray) && CustomParamsArray)
	{
		for (const TSharedPtr<FJsonValue>& ParamValue : *CustomParamsArray)
		{
			const TSharedPtr<FJsonObject> ParamObject = ParamValue.IsValid() ? ParamValue->AsObject() : nullptr;
			if (!ParamObject.IsValid())
			{
				continue;
			}

			OutInfo.CustomParams.Add(TPair<FString, FString>(
				GetJsonStringField_ExportBpy(ParamObject, TEXT("name")),
				GetJsonStringField_ExportBpy(ParamObject, TEXT("type"))));
		}
	}

	const TSharedPtr<FJsonObject>* DefaultsObject = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsObject) && DefaultsObject)
	{
		JsonObjectToStringMap_ExportBpy(*DefaultsObject, OutInfo.DefaultValues);
	}

	const TSharedPtr<FJsonObject>* InputTypesObject = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("input_pin_types"), InputTypesObject) && InputTypesObject)
	{
		JsonObjectToStringMap_ExportBpy(*InputTypesObject, OutInfo.InputPinTypes);
	}

	const TSharedPtr<FJsonObject>* OutputTypesObject = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("output_pin_types"), OutputTypesObject) && OutputTypesObject)
	{
		JsonObjectToStringMap_ExportBpy(*OutputTypesObject, OutInfo.OutputPinTypes);
	}

	const TSharedPtr<FJsonObject>* PinAliasesObject = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("pin_aliases"), PinAliasesObject) && PinAliasesObject)
	{
		JsonObjectToStringMap_ExportBpy(*PinAliasesObject, OutInfo.PinAliases);
	}

	const TSharedPtr<FJsonObject>* PinIdsObject = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("pin_ids"), PinIdsObject) && PinIdsObject)
	{
		JsonObjectToStringMap_ExportBpy(*PinIdsObject, OutInfo.PinIds);
	}

	const TSharedPtr<FJsonObject>* NodePropsObject = nullptr;
	if (NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObject) && NodePropsObject)
	{
		JsonObjectToStringMap_ExportBpy(*NodePropsObject, OutInfo.NodeProps);
	}

	return !OutInfo.NodeType.IsEmpty();
}

FString AppendPosArgToCtorLine_ExportBpy(const FString& CtorLine, const FVector2D& Position)
{
	int32 LastParen = INDEX_NONE;
	if (!CtorLine.FindLastChar(TEXT(')'), LastParen) || LastParen <= 0)
	{
		return CtorLine;
	}

	const FString PosArg = FString::Printf(
		TEXT("pos=(%s, %s)"),
		*FString::SanitizeFloat(Position.X),
		*FString::SanitizeFloat(Position.Y));
	const bool bHasNoArgs = CtorLine[LastParen - 1] == TEXT('(');
	const FString InsertText = bHasNoArgs ? PosArg : (TEXT(", ") + PosArg);
	return CtorLine.Left(LastParen) + InsertText + CtorLine.Mid(LastParen);
}

FString BuildSerializedPinRef_ExportBpy(const FString& NodeVar, const FString& RawPinName)
{
	return BuildPinRefAttribute_ExportBpy(NodeVar, RawPinName, false);
}

FString NodeToCtorLineForSidecar_ExportBpy(const FNodeInfo& Info)
{
	const FString& NodeType = Info.NodeType;

	if (NodeType == TEXT("K2Node_FunctionEntry"))
	{
		return FString::Printf(TEXT("%s = g.entry()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_FunctionResult"))
	{
		return FString::Printf(TEXT("%s = g.result()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_IfThenElse"))
	{
		return FString::Printf(TEXT("%s = g.branch()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_ExecutionSequence"))
	{
		return FString::Printf(TEXT("%s = g.sequence()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_VariableGet"))
	{
		return FString::Printf(TEXT("%s = g.get_var(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_VariableSet"))
	{
		return FString::Printf(TEXT("%s = g.set_var(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_Message"))
	{
		FString FunctionRef = Info.FunctionName;
		if (!Info.ClassName.IsEmpty())
		{
			FunctionRef = Info.ClassName + TEXT("::") + Info.FunctionName;
		}
		return FString::Printf(TEXT("%s = g.message(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(FunctionRef));
	}
	if (Info.bIsCallFunctionLike)
	{
		FString FunctionRef = Info.FunctionName;
		if (!Info.ClassName.IsEmpty())
		{
			FunctionRef = Info.ClassName + TEXT("::") + Info.FunctionName;
		}
		if (NodeType != TEXT("K2Node_CallFunction"))
		{
			return FString::Printf(
				TEXT("%s = g.call(%s, node_class=%s)"),
				*Info.VarName,
				*MakePythonStringLiteral_ExportBpy(FunctionRef),
				*MakePythonStringLiteral_ExportBpy(NodeType));
		}
		return FString::Printf(TEXT("%s = g.call(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(FunctionRef));
	}
	if (NodeType == TEXT("K2Node_Event"))
	{
		return FString::Printf(TEXT("%s = g.event(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_CustomEvent"))
	{
		if (Info.CustomParams.Num() > 0)
		{
			TArray<FString> ParamLiterals;
			ParamLiterals.Reserve(Info.CustomParams.Num());
			for (const TPair<FString, FString>& Param : Info.CustomParams)
			{
				ParamLiterals.Add(FString::Printf(
					TEXT("(%s, %s)"),
					*MakePythonStringLiteral_ExportBpy(Param.Key),
					*MakePythonStringLiteral_ExportBpy(Param.Value)));
			}

			return FString::Printf(
				TEXT("%s = g.custom_event(%s, params=[%s])"),
				*Info.VarName,
				*MakePythonStringLiteral_ExportBpy(Info.FunctionName),
				*FString::Join(ParamLiterals, TEXT(", ")));
		}

		return FString::Printf(TEXT("%s = g.custom_event(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_DynamicCast"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.cast(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
	}
	if (NodeType == TEXT("K2Node_Select"))
	{
		return FString::Printf(TEXT("%s = g.select()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_SwitchEnum"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.switch_enum(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
		return FString::Printf(TEXT("%s = g.switch_enum()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_SwitchInteger"))
	{
		return FString::Printf(TEXT("%s = g.switch_int()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_BreakStruct"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.break_struct(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
		return FString::Printf(TEXT("%s = g.break_struct()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_MakeStruct"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.make_struct(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
		return FString::Printf(TEXT("%s = g.make_struct()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_Self"))
	{
		return FString::Printf(TEXT("%s = g.self_ref()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_CallDelegate"))
	{
		return FString::Printf(TEXT("%s = g.call_dispatcher(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}

	FString TypeStr = NodeType;
	TypeStr.RemoveFromStart(TEXT("K2Node_"));

	FString Args = FString::Printf(TEXT("type=%s"), *MakePythonStringLiteral_ExportBpy(TypeStr));
	if (!Info.FunctionName.IsEmpty())
	{
		Args += FString::Printf(TEXT(", name=%s"), *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (!Info.ClassName.IsEmpty())
	{
		Args += FString::Printf(TEXT(", class_name=%s"), *MakePythonStringLiteral_ExportBpy(Info.ClassName));
	}
	if (!Info.TargetType.IsEmpty())
	{
		Args += FString::Printf(TEXT(", target_type=%s"), *MakePythonStringLiteral_ExportBpy(Info.TargetType));
	}

	return FString::Printf(TEXT("%s = g.node(%s)"), *Info.VarName, *Args);
}

TArray<FString> NodeToDefaultValueLinesForSidecar_ExportBpy(const FNodeInfo& Info)
{
	TArray<FString> Lines;
	for (const TPair<FString, FString>& KV : Info.DefaultValues)
	{
		const FString* PinType = Info.InputPinTypes.Find(KV.Key);
		const FString FormattedLiteral = ForceRealLiteralIfNeeded_ExportBpy(
			FormatPythonValueLiteral_ExportBpy(KV.Value),
			PinType ? *PinType : FString());
		Lines.Add(FString::Printf(
			TEXT("%s.pin(%s, %s)"),
			*Info.VarName,
			*MakePythonStringLiteral_ExportBpy(KV.Key),
			*FormattedLiteral));
	}
	return Lines;
}

void AssignReadableNamesForSidecar_ExportBpy(TArray<FNodeInfo>& Nodes)
{
	TMap<FString, int32> NameCount;
	for (FNodeInfo& Info : Nodes)
	{
		FString BaseName;
		bool bAlwaysNumber = false;

		if (Info.NodeType == TEXT("K2Node_FunctionEntry"))
		{
			BaseName = TEXT("Entry");
		}
		else if (Info.NodeType == TEXT("K2Node_FunctionResult"))
		{
			BaseName = TEXT("Return");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_IfThenElse"))
		{
			BaseName = TEXT("Branch");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_SwitchEnum"))
		{
			BaseName = TEXT("SwitchEnum");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_SwitchInteger"))
		{
			BaseName = TEXT("SwitchInt");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_Select"))
		{
			BaseName = TEXT("Select");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_BreakStruct"))
		{
			BaseName = TEXT("BreakStruct");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_MakeStruct"))
		{
			BaseName = TEXT("MakeStruct");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_DynamicCast"))
		{
			BaseName = TEXT("DynamicCast");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_ExecutionSequence"))
		{
			BaseName = TEXT("Sequence");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_VariableSet"))
		{
			BaseName = TEXT("Set_") + Info.FunctionName;
		}
		else if (!Info.FunctionName.IsEmpty())
		{
			BaseName = Info.FunctionName;
		}
		else
		{
			BaseName = Info.NodeType;
			BaseName.RemoveFromStart(TEXT("K2Node_"));
		}

		const FString SafeBase = SanitizePythonIdentifier_ExportBpy(BaseName, TEXT("Node"));
		int32& Count = NameCount.FindOrAdd(SafeBase);
		if (bAlwaysNumber)
		{
			Info.VarName = FString::Printf(TEXT("%s_%d"), *SafeBase, Count);
		}
		else if (Count == 0)
		{
			Info.VarName = SafeBase;
		}
		else
		{
			Info.VarName = FString::Printf(TEXT("%s_%d"), *SafeBase, Count);
		}
		++Count;
	}
}

bool WriteNestedGraphModule_ExportBpy(
	const FString& OutDir,
	const FString& ModuleStem,
	const TSharedPtr<FJsonObject>& GraphJson,
	FString& OutError);

bool PrepareNestedGraphSidecars_ExportBpy(
	const FString& OutDir,
	const FString& ModuleStem,
	TMap<FString, int32>& InOutStemCounts,
	const TSharedPtr<FJsonObject>& SourceGraphJson,
	TSharedPtr<FJsonObject>& OutGraphJson,
	TMap<FString, TMap<FString, FString>>& OutExternalizedNodeProps,
	FString& OutError)
{
	OutExternalizedNodeProps.Reset();
	if (!CloneJsonObject_ExportBpy(SourceGraphJson, OutGraphJson))
	{
		OutError = FString::Printf(TEXT("Cannot clone nested graph payload for %s"), *ModuleStem);
		return false;
	}

	const TArray<FString> GraphPropKeys = {
		TEXT("BoundGraphJson"),
		TEXT("BlendStackGraphJson"),
		TEXT("StateMachineGraphJson"),
		TEXT("CustomTransitionGraphJson")
	};

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!OutGraphJson->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
	{
		return true;
	}

	for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
	{
		const TSharedPtr<FJsonObject> NodeJson = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
		if (!NodeJson.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* NodePropsPtr = nullptr;
		if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsPtr) || !NodePropsPtr || !(*NodePropsPtr).IsValid())
		{
			continue;
		}
		TSharedPtr<FJsonObject> NodePropsJson = *NodePropsPtr;

		const FString NodeUid = GetJsonStringField_ExportBpy(NodeJson, TEXT("uid"));
		const FString NodeClassName = GetJsonStringField_ExportBpy(NodeJson, TEXT("node_class"));
		for (const FString& PropKey : GraphPropKeys)
		{
			FString NestedGraphText;
			if (!NodePropsJson->TryGetStringField(PropKey, NestedGraphText) || NestedGraphText.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphJson;
			if (!ParseJsonObject_ExportBpy(NestedGraphText, NestedGraphJson) || !NestedGraphJson.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Cannot parse nested graph payload %s on sidecar %s node %s"),
					*PropKey,
					*ModuleStem,
					*NodeUid);
				return false;
			}

			FString NestedGraphName;
			NestedGraphJson->TryGetStringField(TEXT("name"), NestedGraphName);
			if (NestedGraphName.IsEmpty())
			{
				NestedGraphName = PropKey;
			}

			const FString DesiredChildModuleStem = BuildNestedGraphModuleStemForJsonNode_ExportBpy(
				ModuleStem,
				NodeClassName,
				NestedGraphName);
			const FString ChildModuleStem = MakeUniqueNestedGraphModuleStem_ExportBpy(
				DesiredChildModuleStem,
				InOutStemCounts);
			if (!WriteNestedGraphModule_ExportBpy(OutDir, ChildModuleStem, InOutStemCounts, NestedGraphJson, OutError))
			{
				return false;
			}

			NodePropsJson->RemoveField(PropKey);
			OutExternalizedNodeProps.FindOrAdd(NodeUid).Add(PropKey, ChildModuleStem);
		}
	}

	return true;
}

bool WriteNestedGraphModule_ExportBpy(
	const FString& OutDir,
	const FString& ModuleStem,
	TMap<FString, int32>& InOutStemCounts,
	const TSharedPtr<FJsonObject>& GraphJson,
	FString& OutError)
{
	if (!GraphJson.IsValid())
	{
		OutError = TEXT("Cannot write nested graph sidecar: graph json is invalid");
		return false;
	}

	TSharedPtr<FJsonObject> GraphPayloadJson;
	TMap<FString, TMap<FString, FString>> ExternalizedNodeProps;
	if (!PrepareNestedGraphSidecars_ExportBpy(OutDir, ModuleStem, InOutStemCounts, GraphJson, GraphPayloadJson, ExternalizedNodeProps, OutError))
	{
		return false;
	}

	const FString GraphName = GetJsonStringField_ExportBpy(GraphPayloadJson, TEXT("name"), ModuleStem);
	const FString GraphGuid = GetJsonStringField_ExportBpy(GraphPayloadJson, TEXT("graph_guid"));
	const FString GraphOuter = GetJsonStringField_ExportBpy(GraphPayloadJson, TEXT("graph_outer"));
	const FString GraphType = GetJsonStringField_ExportBpy(GraphPayloadJson, TEXT("graph_type"), TEXT("event_graph"));
	const FString GraphCategory = GetJsonStringField_ExportBpy(GraphPayloadJson, TEXT("category"));
	const bool bIsPure = GraphPayloadJson->HasField(TEXT("is_pure")) ? GraphPayloadJson->GetBoolField(TEXT("is_pure")) : false;
	const bool bThreadSafe = GraphPayloadJson->HasField(TEXT("thread_safe")) ? GraphPayloadJson->GetBoolField(TEXT("thread_safe")) : false;

	FString InputsLiteral;
	const TArray<TSharedPtr<FJsonValue>>* InputsArray = nullptr;
	if (GraphPayloadJson->TryGetArrayField(TEXT("inputs"), InputsArray) && InputsArray)
	{
		bool bFirst = true;
		for (const TSharedPtr<FJsonValue>& InputValue : *InputsArray)
		{
			const TSharedPtr<FJsonObject> InputObj = InputValue.IsValid() ? InputValue->AsObject() : nullptr;
			if (!InputObj.IsValid())
			{
				continue;
			}

			if (!bFirst)
			{
				InputsLiteral += TEXT(", ");
			}
			InputsLiteral += FString::Printf(
				TEXT("(%s, %s)"),
				*MakePythonStringLiteral_ExportBpy(GetJsonStringField_ExportBpy(InputObj, TEXT("name"))),
				*MakePythonStringLiteral_ExportBpy(GetJsonStringField_ExportBpy(InputObj, TEXT("type"))));
			bFirst = false;
		}
	}

	FString OutputsLiteral;
	const TArray<TSharedPtr<FJsonValue>>* OutputsArray = nullptr;
	if (GraphPayloadJson->TryGetArrayField(TEXT("outputs"), OutputsArray) && OutputsArray)
	{
		bool bFirst = true;
		for (const TSharedPtr<FJsonValue>& OutputValue : *OutputsArray)
		{
			const TSharedPtr<FJsonObject> OutputObj = OutputValue.IsValid() ? OutputValue->AsObject() : nullptr;
			if (!OutputObj.IsValid())
			{
				continue;
			}

			if (!bFirst)
			{
				OutputsLiteral += TEXT(", ");
			}
			OutputsLiteral += FString::Printf(
				TEXT("(%s, %s)"),
				*MakePythonStringLiteral_ExportBpy(GetJsonStringField_ExportBpy(OutputObj, TEXT("name"))),
				*MakePythonStringLiteral_ExportBpy(GetJsonStringField_ExportBpy(OutputObj, TEXT("type"))));
			bFirst = false;
		}
	}

	TArray<FNodeInfo> NodeInfos;
	TArray<FString> NodeUids;
	TMap<FString, FString> NodeVarByUid;
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (GraphPayloadJson->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray)
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
		{
			const TSharedPtr<FJsonObject> NodeJson = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
			FNodeInfo Info;
			FString NodeUid;
			if (!BuildNodeInfoFromSerializedJson_ExportBpy(NodeJson, Info, NodeUid))
			{
				continue;
			}
			NodeInfos.Add(Info);
			NodeUids.Add(NodeUid);
		}
	}

	AssignReadableNamesForSidecar_ExportBpy(NodeInfos);
	for (int32 Index = 0; Index < NodeInfos.Num() && Index < NodeUids.Num(); ++Index)
	{
		NodeVarByUid.Add(NodeUids[Index], NodeInfos[Index].VarName);
	}

	FString Content;
	Content += TEXT("# Auto-generated by ExportBpy\n");
	Content += TEXT("import importlib.util\n");
	Content += TEXT("import os\n");
	Content += TEXT("from ue_bp_dsl import *\n\n");
	if (!ExternalizedNodeProps.IsEmpty())
	{
		Content += TEXT("def _load_sidecar_graph(stem):\n");
		Content += TEXT("    path = os.path.join(os.path.dirname(__file__), f\"{stem}.bp.py\")\n");
		Content += TEXT("    spec = importlib.util.spec_from_file_location(f\"_exportbpy_sidecar_{stem}\", path)\n");
		Content += TEXT("    if spec is None or spec.loader is None:\n");
		Content += TEXT("        raise ImportError(f\"Cannot load sidecar graph module: {path}\")\n");
		Content += TEXT("    module = importlib.util.module_from_spec(spec)\n");
		Content += TEXT("    spec.loader.exec_module(module)\n");
		Content += TEXT("    return getattr(module, \"GRAPH\")\n\n");
	}
	Content += TEXT("def _build_graph():\n");
	Content += FString::Printf(
		TEXT("    bp = Blueprint(path=%s, parent=%s, bp_type=%s, name=%s)\n"),
		*MakePythonStringLiteral_ExportBpy(TEXT("/Engine/Transient/ExportBpyNested")),
		*MakePythonStringLiteral_ExportBpy(TEXT("/Script/CoreUObject.Object")),
		*MakePythonStringLiteral_ExportBpy(TEXT("Normal")),
		*MakePythonStringLiteral_ExportBpy(GraphName));

	FString ContextHeader;
	if (GraphType == TEXT("function"))
	{
		FString Args = MakePythonStringLiteral_ExportBpy(GraphName);
		if (!GraphGuid.IsEmpty())
		{
			Args += FString::Printf(TEXT(", graph_guid=%s"), *MakePythonStringLiteral_ExportBpy(GraphGuid));
		}
		if (!InputsLiteral.IsEmpty())
		{
			Args += FString::Printf(TEXT(", inputs=[%s]"), *InputsLiteral);
		}
		if (!OutputsLiteral.IsEmpty())
		{
			Args += FString::Printf(TEXT(", outputs=[%s]"), *OutputsLiteral);
		}
		if (bIsPure)
		{
			Args += TEXT(", pure=True");
		}
		if (bThreadSafe)
		{
			Args += TEXT(", thread_safe=True");
		}
		if (!GraphCategory.IsEmpty())
		{
			Args += FString::Printf(TEXT(", category=%s"), *MakePythonStringLiteral_ExportBpy(GraphCategory));
		}
		ContextHeader = FString::Printf(TEXT("with bp.function(%s) as g:"), *Args);
	}
	else if (GraphType == TEXT("macro"))
	{
		FString Args = MakePythonStringLiteral_ExportBpy(GraphName);
		if (!GraphGuid.IsEmpty())
		{
			Args += FString::Printf(TEXT(", graph_guid=%s"), *MakePythonStringLiteral_ExportBpy(GraphGuid));
		}
		if (!InputsLiteral.IsEmpty())
		{
			Args += FString::Printf(TEXT(", inputs=[%s]"), *InputsLiteral);
		}
		if (!OutputsLiteral.IsEmpty())
		{
			Args += FString::Printf(TEXT(", outputs=[%s]"), *OutputsLiteral);
		}
		ContextHeader = FString::Printf(TEXT("with bp.macro(%s) as g:"), *Args);
	}
	else
	{
		FString Args = MakePythonStringLiteral_ExportBpy(GraphName);
		if (!GraphGuid.IsEmpty())
		{
			Args += FString::Printf(TEXT(", graph_guid=%s"), *MakePythonStringLiteral_ExportBpy(GraphGuid));
		}
		ContextHeader = FString::Printf(TEXT("with bp.event_graph(%s) as g:"), *Args);
	}
	Content += TEXT("    ") + ContextHeader + TEXT("\n");

	if (NodeInfos.IsEmpty())
	{
		Content += TEXT("        pass\n");
	}
	else
	{
		Content += TEXT("        # Nodes\n");
		for (int32 Index = 0; Index < NodeInfos.Num(); ++Index)
		{
			const FNodeInfo& Info = NodeInfos[Index];
			const FString& NodeUid = NodeUids.IsValidIndex(Index) ? NodeUids[Index] : FString();
			Content += TEXT("        ") + AppendPosArgToCtorLine_ExportBpy(NodeToCtorLineForSidecar_ExportBpy(Info), Info.Position) + TEXT("\n");
			if (!Info.NodeGuid.IsEmpty())
			{
				Content += FString::Printf(
					TEXT("        %s.set_node_guid(%s)\n"),
					*Info.VarName,
					*MakePythonStringLiteral_ExportBpy(Info.NodeGuid));
			}

			for (const FString& DefaultLine : NodeToDefaultValueLinesForSidecar_ExportBpy(Info))
			{
				Content += TEXT("        ") + DefaultLine + TEXT("\n");
			}

			auto AppendSortedSetterLines = [&](const TMap<FString, FString>& Map, const FString& MethodName)
			{
				TArray<FString> Keys;
				Map.GetKeys(Keys);
				Keys.Sort();
				for (const FString& Key : Keys)
				{
					const FString* Value = Map.Find(Key);
					if (!Value)
					{
						continue;
					}

					Content += FString::Printf(
						TEXT("        %s.%s(%s, %s)\n"),
						*Info.VarName,
						*MethodName,
						*MakePythonStringLiteral_ExportBpy(Key),
						*MakePythonStringLiteral_ExportBpy(*Value));
				}
			};

			AppendSortedSetterLines(Info.InputPinTypes, TEXT("set_input_pin_type"));
			AppendSortedSetterLines(Info.OutputPinTypes, TEXT("set_output_pin_type"));
			AppendSortedSetterLines(Info.PinAliases, TEXT("set_pin_alias"));
			AppendSortedSetterLines(Info.PinIds, TEXT("set_pin_id"));

			TArray<FString> PropKeys;
			Info.NodeProps.GetKeys(PropKeys);
			PropKeys.Sort();
			for (const FString& PropKey : PropKeys)
			{
				const FString* PropValue = Info.NodeProps.Find(PropKey);
				if (!PropValue)
				{
					continue;
				}

				Content += FString::Printf(
					TEXT("        %s.set_extra_prop(%s, %s)\n"),
					*Info.VarName,
					*MakePythonStringLiteral_ExportBpy(PropKey),
					*FormatPythonValueLiteral_ExportBpy(*PropValue));
			}

			if (const TMap<FString, FString>* NestedProps = ExternalizedNodeProps.Find(NodeUid))
			{
				TArray<FString> NestedKeys;
				NestedProps->GetKeys(NestedKeys);
				NestedKeys.Sort();
				for (const FString& PropKey : NestedKeys)
				{
					const FString* ModuleName = NestedProps->Find(PropKey);
					if (!ModuleName || ModuleName->IsEmpty())
					{
						continue;
					}

					Content += FString::Printf(
						TEXT("        %s.set_extra_prop(%s, nested_graph_prop(_load_sidecar_graph(%s)))\n"),
						*Info.VarName,
						*MakePythonStringLiteral_ExportBpy(PropKey),
						*MakePythonStringLiteral_ExportBpy(*ModuleName));
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	if (GraphPayloadJson->TryGetArrayField(TEXT("connections"), ConnectionsArray) && ConnectionsArray && ConnectionsArray->Num() > 0)
	{
		Content += TEXT("\n        # Connections\n");
		TSet<FString> SeenConnections;
		for (const TSharedPtr<FJsonValue>& ConnectionValue : *ConnectionsArray)
		{
			const TSharedPtr<FJsonObject> ConnectionObject = ConnectionValue.IsValid() ? ConnectionValue->AsObject() : nullptr;
			if (!ConnectionObject.IsValid())
			{
				continue;
			}

			const FString SrcUid = GetJsonStringField_ExportBpy(ConnectionObject, TEXT("src_node"));
			const FString DstUid = GetJsonStringField_ExportBpy(ConnectionObject, TEXT("dst_node"));
			const FString SrcPin = GetJsonStringField_ExportBpy(ConnectionObject, TEXT("src_pin"));
			const FString DstPin = GetJsonStringField_ExportBpy(ConnectionObject, TEXT("dst_pin"));
			const FString* SrcVar = NodeVarByUid.Find(SrcUid);
			const FString* DstVar = NodeVarByUid.Find(DstUid);
			if (!SrcVar || !DstVar || SrcPin.IsEmpty() || DstPin.IsEmpty())
			{
				continue;
			}

			const FString ConnectionLine =
				BuildSerializedPinRef_ExportBpy(*SrcVar, SrcPin) +
				TEXT(" >> ") +
				BuildSerializedPinRef_ExportBpy(*DstVar, DstPin);
			if (SeenConnections.Contains(ConnectionLine))
			{
				continue;
			}
			SeenConnections.Add(ConnectionLine);
			Content += TEXT("        ") + ConnectionLine + TEXT("\n");
		}
	}

	Content += TEXT("\n");
	Content += TEXT("    graph = bp.to_dict()[\"graphs\"][0]\n");
	Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
		*MakePythonStringLiteral_ExportBpy(TEXT("name")),
		*MakePythonStringLiteral_ExportBpy(GraphName));
	Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
		*MakePythonStringLiteral_ExportBpy(TEXT("graph_type")),
		*MakePythonStringLiteral_ExportBpy(GraphType));
	if (InputsArray && InputsArray->Num() > 0)
	{
		Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
			*MakePythonStringLiteral_ExportBpy(TEXT("inputs")),
			*JsonArrayToPythonLiteral_ExportBpy(*InputsArray, 0));
	}
	if (OutputsArray && OutputsArray->Num() > 0)
	{
		Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
			*MakePythonStringLiteral_ExportBpy(TEXT("outputs")),
			*JsonArrayToPythonLiteral_ExportBpy(*OutputsArray, 0));
	}
	if (!GraphGuid.IsEmpty())
	{
		Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
			*MakePythonStringLiteral_ExportBpy(TEXT("graph_guid")),
			*MakePythonStringLiteral_ExportBpy(GraphGuid));
	}
	if (!GraphOuter.IsEmpty())
	{
		Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
			*MakePythonStringLiteral_ExportBpy(TEXT("graph_outer")),
			*MakePythonStringLiteral_ExportBpy(GraphOuter));
	}
	if (!GraphCategory.IsEmpty())
	{
		Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
			*MakePythonStringLiteral_ExportBpy(TEXT("category")),
			*MakePythonStringLiteral_ExportBpy(GraphCategory));
	}
	Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
		*MakePythonStringLiteral_ExportBpy(TEXT("is_pure")),
		bIsPure ? TEXT("True") : TEXT("False"));
	Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
		*MakePythonStringLiteral_ExportBpy(TEXT("thread_safe")),
		bThreadSafe ? TEXT("True") : TEXT("False"));

	const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
	if (GraphPayloadJson->TryGetObjectField(TEXT("metadata"), MetadataObject) && MetadataObject && (*MetadataObject).IsValid() && (*MetadataObject)->Values.Num() > 0)
	{
		Content += FString::Printf(TEXT("    graph[%s] = %s\n"),
			*MakePythonStringLiteral_ExportBpy(TEXT("metadata")),
			*JsonObjectToPythonLiteral_ExportBpy(*MetadataObject, 0));
	}
	Content += TEXT("    return graph\n\n");
	Content += TEXT("GRAPH = _build_graph()\n");

	const FString OutPath = FPaths::Combine(OutDir, ModuleStem + TEXT(".bp.py"));
	if (!FFileHelper::SaveStringToFile(Content, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Cannot write nested graph sidecar %s"), *OutPath);
		return false;
	}

	return true;
}
FString BuildModuleName_ExportBpy(const FString& Prefix, const FString& GraphName)
{
	return Prefix + SanitizePythonIdentifier_ExportBpy(GraphName, TEXT("graph"));
}

enum class ERootGraphKind_ExportBpy : uint8
{
	Macro = 0,
	Function = 1,
	Event = 2
};

ERootGraphKind_ExportBpy ClassifyRootGraphKind_ExportBpy(UBlueprint* BP, UEdGraph* Graph)
{
	if (!Graph)
	{
		return ERootGraphKind_ExportBpy::Event;
	}

	if (BP)
	{
		if (BP->MacroGraphs.Contains(Graph))
		{
			return ERootGraphKind_ExportBpy::Macro;
		}
		if (BP->FunctionGraphs.Contains(Graph))
		{
			return ERootGraphKind_ExportBpy::Function;
		}
		if (BP->UbergraphPages.Contains(Graph))
		{
			return ERootGraphKind_ExportBpy::Event;
		}
	}

	TArray<UK2Node_FunctionEntry*> EntryNodes;
	Graph->GetNodesOfClass(EntryNodes);
	if (EntryNodes.Num() > 0)
	{
		return ERootGraphKind_ExportBpy::Function;
	}

	TArray<UK2Node_Tunnel*> TunnelNodes;
	Graph->GetNodesOfClass(TunnelNodes);
	if (TunnelNodes.Num() > 0)
	{
		return ERootGraphKind_ExportBpy::Macro;
	}

	return ERootGraphKind_ExportBpy::Event;
}

FString GetRootGraphPrefix_ExportBpy(UBlueprint* BP, UEdGraph* Graph)
{
	switch (ClassifyRootGraphKind_ExportBpy(BP, Graph))
	{
	case ERootGraphKind_ExportBpy::Macro:
		return TEXT("macro_");
	case ERootGraphKind_ExportBpy::Function:
		return TEXT("fn_");
	default:
		return TEXT("evt_");
	}
}

FString GetRootGraphType_ExportBpy(UBlueprint* BP, UEdGraph* Graph)
{
	switch (ClassifyRootGraphKind_ExportBpy(BP, Graph))
	{
	case ERootGraphKind_ExportBpy::Macro:
		return TEXT("macro");
	case ERootGraphKind_ExportBpy::Function:
		return TEXT("function");
	default:
		return TEXT("event_graph");
	}
}

FString ResolveFunctionCategory_ExportBpy(UEdGraph* Graph, const UK2Node_FunctionEntry* EntryNode)
{
	FString FunctionCategory;
	if (EntryNode)
	{
		FunctionCategory = EntryNode->MetaData.Category.ToString();
		if (FunctionCategory.IsEmpty())
		{
			if (const UFunction* SignatureFunction = EntryNode->FindSignatureFunction())
			{
				FunctionCategory = SignatureFunction->GetMetaData(FBlueprintMetadata::MD_FunctionCategory);
			}
		}
	}

	if (FunctionCategory.IsEmpty() && Graph)
	{
		if (const UEdGraphSchema* Schema = Graph->GetSchema())
		{
			const FString SchemaCategory = Schema->GetGraphCategory(Graph).ToString();
			if (!SchemaCategory.IsEmpty() &&
				!SchemaCategory.Equals(UEdGraphSchema_K2::VR_DefaultCategory.ToString(), ESearchCase::CaseSensitive))
			{
				FunctionCategory = SchemaCategory;
			}
		}
	}

	return FunctionCategory;
}

void CollectRootGraphs_ExportBpy(UBlueprint* BP, TArray<UEdGraph*>& OutGraphs)
{
	OutGraphs.Reset();
	if (!BP)
	{
		return;
	}

	TSet<UEdGraph*> SeenGraphs;
	auto TryAddGraph = [&](UEdGraph* Graph)
	{
		if (!Graph || SeenGraphs.Contains(Graph))
		{
			return;
		}
		if (Graph->GetOuter() != BP)
		{
			return;
		}
		if (BP->DelegateSignatureGraphs.Contains(Graph))
		{
			return;
		}

		SeenGraphs.Add(Graph);
		OutGraphs.Add(Graph);
	};

	auto CollectArray = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs)
	{
		for (UEdGraph* Graph : Graphs)
		{
			TryAddGraph(Graph);
		}
	};

	CollectArray(BP->MacroGraphs);
	CollectArray(BP->FunctionGraphs);
	CollectArray(BP->UbergraphPages);

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		TryAddGraph(Graph);
	}

	OutGraphs.Sort([BP](const UEdGraph& A, const UEdGraph& B)
	{
		const ERootGraphKind_ExportBpy KindA =
			ClassifyRootGraphKind_ExportBpy(BP, const_cast<UEdGraph*>(&A));
		const ERootGraphKind_ExportBpy KindB =
			ClassifyRootGraphKind_ExportBpy(BP, const_cast<UEdGraph*>(&B));
		if (KindA != KindB)
		{
			return static_cast<uint8>(KindA) < static_cast<uint8>(KindB);
		}
		return A.GetName() < B.GetName();
	});
}

void CollectGraphModuleNames_ExportBpy(UBlueprint* BP, TArray<FString>& OutGraphModules)
{
	OutGraphModules.Reset();
	if (!BP)
	{
		return;
	}

	TArray<UEdGraph*> RootGraphs;
	CollectRootGraphs_ExportBpy(BP, RootGraphs);
	for (UEdGraph* Graph : RootGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		OutGraphModules.Add(BuildModuleName_ExportBpy(GetRootGraphPrefix_ExportBpy(BP, Graph), Graph->GetName()));
	}
}

FString BuildPinRefFallback_ExportBpy(const FString& NodeVar, const FString& RawPinName)
{
	return FString::Printf(TEXT("%s[%s]"), *NodeVar, *MakePythonStringLiteral_ExportBpy(RawPinName));
}

FString BuildPinRefAttribute_ExportBpy(const FString& NodeVar, const FString& RawPinName, bool bAllowGuidCleanup)
{
	const FString CleanPinName = bAllowGuidCleanup ? StripGuidSuffix_ExportBpy(RawPinName) : RawPinName;
	if (CleanPinName == TEXT("self"))
	{
		return NodeVar + TEXT(".self_");
	}
	if (CanUseAttributeSyntax_ExportBpy(CleanPinName))
	{
		return NodeVar + TEXT(".") + CleanPinName;
	}
	return BuildPinRefFallback_ExportBpy(NodeVar, RawPinName);
}

FString TranslateOutputPinRef_ExportBpy(UK2Node* Node, const FString& NodeVar, UEdGraphPin* Pin)
{
	const FString PinName = Pin ? Pin->PinName.ToString() : FString();
	if (!Pin)
	{
		return NodeVar;
	}

	// Keep reroute pins explicit as InputPin/OutputPin. Mapping them to generic
	// exec aliases loses compatibility with K2Node_Knot import pin resolution.
	if (Node->IsA<UK2Node_Knot>())
	{
		return BuildPinRefAttribute_ExportBpy(NodeVar, PinName, false);
	}

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		if (Node->IsA<UK2Node_IfThenElse>())
		{
			if (PinName == UEdGraphSchema_K2::PN_Then.ToString())
			{
				return NodeVar + TEXT(".true_");
			}
			if (PinName == UEdGraphSchema_K2::PN_Else.ToString())
			{
				return NodeVar + TEXT(".false_");
			}
		}

		if (Node->IsA<UK2Node_DynamicCast>() && PinName == TEXT("CastFailed"))
		{
			return NodeVar + TEXT(".cast_failed");
		}

		if (Node->IsA<UK2Node_SwitchEnum>())
		{
			return FString::Printf(TEXT("%s.case(%s)"), *NodeVar, *MakePythonStringLiteral_ExportBpy(PinName));
		}

		if (Node->IsA<UK2Node_SwitchInteger>())
		{
			int32 CaseValue = 0;
			if (LexTryParseString(CaseValue, *PinName))
			{
				return FString::Printf(TEXT("%s.case(%d)"), *NodeVar, CaseValue);
			}
			return FString::Printf(TEXT("%s.case(%s)"), *NodeVar, *MakePythonStringLiteral_ExportBpy(PinName));
		}

		if (PinName == UEdGraphSchema_K2::PN_Then.ToString())
		{
			return NodeVar + TEXT(".then");
		}

		return BuildPinRefFallback_ExportBpy(NodeVar, PinName);
	}

	if (Node->IsA<UK2Node_VariableGet>())
	{
		const FString LogicalPinName = GetLogicalPinName_ExportBpy(Node, Pin);
		if (const UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
		{
			const FString VariableName = VariableGet->VariableReference.GetMemberName().ToString();
			if (LogicalPinName == VariableName)
			{
				return NodeVar + TEXT(".value");
			}
		}
		return BuildPinRefAttribute_ExportBpy(NodeVar, LogicalPinName, false);
	}

	if (PinName == UEdGraphSchema_K2::PN_ReturnValue.ToString())
	{
		return NodeVar + TEXT(".result");
	}

	if (Node->IsA<UK2Node_BreakStruct>() || Node->IsA<UK2Node_MakeStruct>())
	{
		return BuildPinRefAttribute_ExportBpy(NodeVar, PinName, true);
	}

	return BuildPinRefAttribute_ExportBpy(NodeVar, PinName, false);
}

FString TranslateInputPinRef_ExportBpy(UK2Node* Node, const FString& NodeVar, UEdGraphPin* Pin)
{
	const FString PinName = Pin ? Pin->PinName.ToString() : FString();
	if (!Pin)
	{
		return NodeVar;
	}

	// Keep reroute pins explicit as InputPin/OutputPin. Mapping them to generic
	// exec aliases loses compatibility with K2Node_Knot import pin resolution.
	if (Node->IsA<UK2Node_Knot>())
	{
		return BuildPinRefAttribute_ExportBpy(NodeVar, PinName, false);
	}

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return NodeVar + TEXT(".exec");
	}

	if (Node->IsA<UK2Node_IfThenElse>() && PinName == UEdGraphSchema_K2::PN_Condition.ToString())
	{
		return NodeVar + TEXT(".condition");
	}

	if ((Node->IsA<UK2Node_SwitchEnum>() || Node->IsA<UK2Node_SwitchInteger>()) &&
		PinName == TEXT("Selection"))
	{
		return NodeVar + TEXT(".selection");
	}

	if (Node->IsA<UK2Node_Select>() && PinName == UEdGraphSchema_K2::PN_Index.ToString())
	{
		return NodeVar + TEXT(".index");
	}

	return BuildPinRefAttribute_ExportBpy(NodeVar, PinName, false);
}

UClass* ResolveLinkedAnimLayerInterfaceClass_ExportBpy(const UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode)
{
	if (!LinkedLayerNode)
	{
		return nullptr;
	}

	if (UClass* ExplicitClass = LinkedLayerNode->Node.Interface.Get())
	{
		return ExplicitClass->GetAuthoritativeClass();
	}

	return nullptr;
}

FNodeInfo BuildNodeInfo_ExportBpy(UK2Node* Node)
{
	FNodeInfo Info;
	Info.NodeType = Node->GetClass()->GetName();
	Info.Position = FVector2D(Node->NodePosX, Node->NodePosY);
	Info.NodeGuid = Node->NodeGuid.ToString(EGuidFormats::Digits);

	if (const UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(Node))
	{
		Info.FunctionName = FE->CustomGeneratedFunctionName.ToString();
	}
	else if (Cast<UK2Node_FunctionResult>(Node))
	{
		Info.FunctionName = TEXT("return");
	}
	else if (const UK2Node_Message* MessageNode = Cast<UK2Node_Message>(Node))
	{
		Info.FunctionName = MessageNode->FunctionReference.GetMemberName().ToString();
		if (UClass* OwnerClass = MessageNode->FunctionReference.GetMemberParentClass())
		{
			Info.ClassName = OwnerClass->GetName();
			Info.NodeProps.Add(TEXT("InterfaceClass"), OwnerClass->GetPathName());
		}
	}
	else if (const UK2Node_CallFunction* Fn = Cast<UK2Node_CallFunction>(Node))
	{
		Info.bIsCallFunctionLike = true;
		Info.FunctionName = Fn->FunctionReference.GetMemberName().ToString();
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("NodePurityOverride"));
		if (!Fn->FunctionReference.IsSelfContext())
		{
			if (UClass* OwnerClass = Fn->FunctionReference.GetMemberParentClass())
			{
				UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
				const bool bOwnerIsCurrentBlueprint =
					OwningBlueprint &&
					OwnerClass->ClassGeneratedBy == OwningBlueprint;

				if (!bOwnerIsCurrentBlueprint)
				{
					Info.ClassName = OwnerClass->GetName();
					Info.NodeProps.Add(TEXT("FunctionOwnerClass"), OwnerClass->GetPathName());
				}
			}
		}

		if (const UK2Node_AnimGetter* AnimGetterNode = Cast<UK2Node_AnimGetter>(Node))
		{
			const FString SourceNodeUid = GetReferencedNodeSerializedUid_ExportBpy(AnimGetterNode->SourceNode);
			if (!SourceNodeUid.IsEmpty())
			{
				Info.NodeProps.Add(TEXT("AnimGetterSourceNodeUid"), SourceNodeUid);
			}
			const FString SourceStateNodeUid =
				GetReferencedNodeSerializedUid_ExportBpy(AnimGetterNode->SourceStateNode);
			if (!SourceStateNodeUid.IsEmpty())
			{
				Info.NodeProps.Add(TEXT("AnimGetterSourceStateNodeUid"), SourceStateNodeUid);
			}
			if (AnimGetterNode->GetterClass)
			{
				Info.NodeProps.Add(TEXT("AnimGetterClass"), AnimGetterNode->GetterClass->GetPathName());
			}
			if (AnimGetterNode->SourceAnimBlueprint)
			{
				Info.NodeProps.Add(TEXT("AnimGetterSourceBlueprint"), AnimGetterNode->SourceAnimBlueprint->GetPathName());
			}
			if (AnimGetterNode->Contexts.Num() > 0)
			{
				Info.NodeProps.Add(TEXT("AnimGetterContexts"), FString::Join(AnimGetterNode->Contexts, TEXT("|")));
			}

			const FString CachedTitle = AnimGetterNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			if (!CachedTitle.IsEmpty())
			{
				Info.NodeProps.Add(TEXT("AnimGetterTitle"), CachedTitle);
			}
		}
	}
	else if (const UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(Node))
	{
		Info.FunctionName = VG->VariableReference.GetMemberName().ToString();
		Info.NodeProps.Add(TEXT("VariableGetIsPure"), VG->IsNodePure() ? TEXT("true") : TEXT("false"));
	}
	else if (const UK2Node_VariableSet* VS = Cast<UK2Node_VariableSet>(Node))
	{
		Info.FunctionName = VS->VariableReference.GetMemberName().ToString();
	}
	else if (const UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node))
	{
		Info.FunctionName = CE->CustomFunctionName.ToString();
		Info.NodeProps.Add(TEXT("NetFlags"), LexToString(static_cast<uint64>(CE->GetNetFlags())));
		Info.NodeProps.Add(TEXT("CallInEditor"), CE->bCallInEditor ? TEXT("true") : TEXT("false"));
		Info.NodeProps.Add(TEXT("IsDeprecated"), CE->bIsDeprecated ? TEXT("true") : TEXT("false"));
		if (!CE->DeprecationMessage.IsEmpty())
		{
			Info.NodeProps.Add(TEXT("DeprecationMessage"), CE->DeprecationMessage);
		}
	}
	else if (const UK2Node_Event* Evt = Cast<UK2Node_Event>(Node))
	{
		Info.FunctionName = Evt->EventReference.GetMemberName().ToString();
	}
	else if (const UK2Node_MacroInstance* MI = Cast<UK2Node_MacroInstance>(Node))
	{
		if (UEdGraph* MacroGraph = MI->GetMacroGraph())
		{
			Info.FunctionName = MacroGraph->GetName();
			Info.TargetType = MacroGraph->GetPathName();
			Info.NodeProps.Add(TEXT("MacroGraph"), MacroGraph->GetPathName());
		}
	}
	else if (const UK2Node_BaseMCDelegate* DelegateNode = Cast<UK2Node_BaseMCDelegate>(Node))
	{
		Info.FunctionName = DelegateNode->GetPropertyName().ToString();
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("DelegateReference"));
	}
	else if (const UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
	{
		Info.FunctionName = CreateDelegateNode->GetFunctionName().ToString();
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("SelectedFunctionName"));
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("SelectedFunctionGuid"));
	}
	else if (const UK2Node_GetSubsystem* GetSubsystemNode = Cast<UK2Node_GetSubsystem>(Node))
	{
		UClass* SubsystemClass = nullptr;

		if (UEdGraphPin* ResultPin = GetSubsystemNode->GetResultPin())
		{
			SubsystemClass = Cast<UClass>(ResultPin->PinType.PinSubCategoryObject.Get());
		}

		if (!SubsystemClass)
		{
			if (UEdGraphPin* ClassPin = GetSubsystemNode->GetClassPin())
			{
				SubsystemClass = Cast<UClass>(ClassPin->DefaultObject);
			}
		}

		if (SubsystemClass)
		{
			Info.TargetType = SubsystemClass->GetPathName();
			Info.NodeProps.Add(TEXT("CustomClass"), SubsystemClass->GetPathName());
		}
	}

	if (const UK2Node_TransitionRuleGetter* TransitionGetterNode = Cast<UK2Node_TransitionRuleGetter>(Node))
	{
		Info.NodeProps.Add(
			TEXT("TransitionGetterType"),
			FString::FromInt(static_cast<int32>(TransitionGetterNode->GetterType.GetValue())));

		const FString AssociatedStateUid =
			GetReferencedNodeSerializedUid_ExportBpy(TransitionGetterNode->AssociatedStateNode);
		if (!AssociatedStateUid.IsEmpty())
		{
			Info.NodeProps.Add(TEXT("TransitionAssociatedStateNodeUid"), AssociatedStateUid);
		}
		const FString AssociatedAnimNodeUid =
			GetReferencedNodeSerializedUid_ExportBpy(TransitionGetterNode->AssociatedAnimAssetPlayerNode);
		if (!AssociatedAnimNodeUid.IsEmpty())
		{
			Info.NodeProps.Add(TEXT("TransitionAssociatedAnimAssetPlayerNodeUid"), AssociatedAnimNodeUid);
		}
	}

	if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		if (CastNode->TargetType)
		{
			Info.TargetType = CastNode->TargetType->GetPathName();
			Info.NodeProps.Add(TEXT("TargetType"), CastNode->TargetType->GetPathName());
		}
		Info.NodeProps.Add(TEXT("CastIsPure"), CastNode->IsNodePure() ? TEXT("true") : TEXT("false"));
	}
	else if (const UK2Node_EnumEquality* EnumEqualityNode = Cast<UK2Node_EnumEquality>(Node))
	{
		auto TryCaptureEnumFromPin = [&Info](const UEdGraphPin* Pin) -> bool
		{
			if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Byte)
			{
				return false;
			}

			const UEnum* EnumObject = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
			if (!EnumObject)
			{
				return false;
			}

			Info.TargetType = EnumObject->GetPathName();
			Info.NodeProps.Add(TEXT("Enum"), EnumObject->GetPathName());
			return true;
		};

		TryCaptureEnumFromPin(EnumEqualityNode->GetInput1Pin()) ||
			TryCaptureEnumFromPin(EnumEqualityNode->GetInput2Pin());
	}
	else if (const UAnimGraphNode_BlendListByEnum* BlendListByEnumNode = Cast<UAnimGraphNode_BlendListByEnum>(Node))
	{
		if (UEnum* EnumObject = BlendListByEnumNode->GetEnum())
		{
			Info.TargetType = EnumObject->GetPathName();
			Info.NodeProps.Add(TEXT("Enum"), EnumObject->GetPathName());
		}

		TArray<FString> VisibleEnumEntries;
		if (const FArrayProperty* VisibleEnumEntriesProperty =
				FindFProperty<FArrayProperty>(UAnimGraphNode_BlendListByEnum::StaticClass(), TEXT("VisibleEnumEntries")))
		{
			if (const FNameProperty* VisibleEnumEntryProperty = CastField<FNameProperty>(VisibleEnumEntriesProperty->Inner))
			{
				FScriptArrayHelper VisibleEnumEntriesHelper(
					VisibleEnumEntriesProperty,
					VisibleEnumEntriesProperty->ContainerPtrToValuePtr<void>(
						const_cast<UAnimGraphNode_BlendListByEnum*>(BlendListByEnumNode)));
				VisibleEnumEntries.Reserve(VisibleEnumEntriesHelper.Num());

				for (int32 EntryIndex = 0; EntryIndex < VisibleEnumEntriesHelper.Num(); ++EntryIndex)
				{
					const FName VisibleEnumEntry =
						VisibleEnumEntryProperty->GetPropertyValue(VisibleEnumEntriesHelper.GetRawPtr(EntryIndex));
					if (!VisibleEnumEntry.IsNone())
					{
						VisibleEnumEntries.Add(VisibleEnumEntry.ToString());
					}
				}
			}
		}

		if (VisibleEnumEntries.Num() > 0)
		{
			Info.NodeProps.Add(TEXT("VisibleEnumEntries"), FString::Join(VisibleEnumEntries, TEXT("|")));
		}
	}
	else if (const UAnimGraphNode_SaveCachedPose* SaveCachedPoseNode = Cast<UAnimGraphNode_SaveCachedPose>(Node))
	{
		if (!SaveCachedPoseNode->CacheName.IsEmpty())
		{
			Info.NodeProps.Add(TEXT("CacheName"), SaveCachedPoseNode->CacheName);
		}
	}
	else if (const UAnimGraphNode_UseCachedPose* UseCachedPoseNode = Cast<UAnimGraphNode_UseCachedPose>(Node))
	{
		FString CachePoseName;
		if (!UseCachedPoseNode->Node.CachePoseName.IsNone())
		{
			CachePoseName = UseCachedPoseNode->Node.CachePoseName.ToString();
		}
		else if (UseCachedPoseNode->SaveCachedPoseNode.IsValid())
		{
			CachePoseName = UseCachedPoseNode->SaveCachedPoseNode->CacheName;
		}
		else if (const FStrProperty* NameOfCacheProperty =
				FindFProperty<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass(), TEXT("NameOfCache")))
		{
			CachePoseName = NameOfCacheProperty->GetPropertyValue_InContainer(UseCachedPoseNode);
		}

		if (!CachePoseName.IsEmpty())
		{
			Info.NodeProps.Add(TEXT("CacheName"), CachePoseName);
		}
	}
	else if (const UK2Node_SwitchEnum* SwitchEnumNode = Cast<UK2Node_SwitchEnum>(Node))
	{
		if (SwitchEnumNode->GetEnum())
		{
			Info.TargetType = SwitchEnumNode->GetEnum()->GetPathName();
			Info.NodeProps.Add(TEXT("Enum"), SwitchEnumNode->GetEnum()->GetPathName());
		}
	}
	else if (const UK2Node_StructOperation* StructNode = Cast<UK2Node_StructOperation>(Node))
	{
		if (StructNode->StructType)
		{
			Info.TargetType = StructNode->StructType->GetPathName();
			Info.NodeProps.Add(TEXT("StructType"), StructNode->StructType->GetPathName());
		}
	}

	if (const UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node))
	{
		TArray<FString> VisiblePins;
		for (const FOptionalPinFromProperty& Record : BreakStructNode->ShowPinForProperties)
		{
			if (Record.bShowPin)
			{
				VisiblePins.Add(Record.PropertyName.ToString());
			}
		}

		Info.NodeProps.Add(TEXT("VisiblePins"), FString::Join(VisiblePins, TEXT("|")));
	}
	else if (const UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
	{
		TArray<FString> VisiblePins;
		for (const FOptionalPinFromProperty& Record : MakeStructNode->ShowPinForProperties)
		{
			if (Record.bShowPin)
			{
				VisiblePins.Add(Record.PropertyName.ToString());
			}
		}

		Info.NodeProps.Add(TEXT("VisiblePins"), FString::Join(VisiblePins, TEXT("|")));
	}

	if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
	{
		const FMemberReference& VariableReference = VariableNode->VariableReference;
		const FString VariableScope = VariableReference.IsLocalScope()
			? TEXT("Local")
			: (VariableReference.IsSelfContext() ? TEXT("Self") : TEXT("External"));
		Info.NodeProps.Add(TEXT("VariableScope"), VariableScope);

		if (VariableReference.GetMemberGuid().IsValid())
		{
			Info.NodeProps.Add(TEXT("VariableGuid"), VariableReference.GetMemberGuid().ToString(EGuidFormats::Digits));
		}
		if (VariableReference.IsLocalScope() && !VariableReference.GetMemberScopeName().IsEmpty())
		{
			Info.NodeProps.Add(TEXT("VariableScopeName"), VariableReference.GetMemberScopeName());
		}
		if (!VariableReference.IsLocalScope() && !VariableReference.IsSelfContext())
		{
			if (UClass* OwnerClass = VariableReference.GetMemberParentClass(Node->GetBlueprintClassFromNode()))
			{
				Info.NodeProps.Add(TEXT("VariableOwnerClass"), OwnerClass->GetPathName());
			}
		}

		if (const FProperty* VariableProperty = VariableNode->GetPropertyForVariable())
		{
			FEdGraphPinType VariablePinType;
			if (const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
				Schema && Schema->ConvertPropertyToPinType(VariableProperty, VariablePinType))
			{
				Info.NodeProps.Add(TEXT("VariableType"), NormalizeTypeString_ExportBpy(VariablePinType));
				Info.NodeProps.Add(TEXT("VariableContainer"), GetPinContainerString_ExportBpy(VariablePinType));
			}

			Info.NodeProps.Add(
				TEXT("VariableKind"),
				VariableProperty->HasAnyPropertyFlags(CPF_Parm)
					? TEXT("Parameter")
					: (VariableReference.IsLocalScope() ? TEXT("Local") : TEXT("Member")));
		}
	}

	if (const UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
	{
		if (UEnum* EnumObject = SelectNode->GetEnum())
		{
			Info.NodeProps.Add(TEXT("Enum"), EnumObject->GetPathName());
		}

		if (UEdGraphPin* IndexPin = SelectNode->GetIndexPin())
		{
			Info.NodeProps.Add(TEXT("IndexType"), NormalizeTypeString_ExportBpy(IndexPin->PinType));
			Info.NodeProps.Add(TEXT("IndexContainer"), GetPinContainerString_ExportBpy(IndexPin->PinType));
		}

		const UEdGraphPin* ValuePin = SelectNode->GetReturnValuePin();
		if (ValuePin && ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			TArray<UEdGraphPin*> OptionPins;
			SelectNode->GetOptionPins(OptionPins);
			for (UEdGraphPin* OptionPin : OptionPins)
			{
				if (OptionPin && OptionPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
				{
					ValuePin = OptionPin;
					break;
				}
			}
		}

		if (ValuePin)
		{
			Info.NodeProps.Add(TEXT("ValueType"), NormalizeTypeString_ExportBpy(ValuePin->PinType));
			Info.NodeProps.Add(TEXT("ValueContainer"), GetPinContainerString_ExportBpy(ValuePin->PinType));
		}
	}

	if (const UK2Node_SetFieldsInStruct* SetFieldsNode = Cast<UK2Node_SetFieldsInStruct>(Node))
	{
		TArray<FString> VisiblePins;
		for (const FOptionalPinFromProperty& Record : SetFieldsNode->ShowPinForProperties)
		{
			if (Record.bShowPin)
			{
				VisiblePins.Add(Record.PropertyName.ToString());
			}
		}

		Info.NodeProps.Add(TEXT("VisiblePins"), FString::Join(VisiblePins, TEXT("|")));
	}

	if (const UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
	{
		if (StateMachineNode->EditorStateMachineGraph)
		{
			Info.NodeProps.Add(
				TEXT("StateMachineGraphJson"),
				SerializeJsonCompact_ExportBpy(UBPDirectExporter::SerializeGraph(StateMachineNode->EditorStateMachineGraph)));
		}
	}

	if (UEdGraph* BlendStackGraph = ResolveBlendStackGraph_ExportBpy(Node))
	{
		Info.NodeProps.Add(
			TEXT("BlendStackGraphJson"),
			SerializeJsonCompact_ExportBpy(UBPDirectExporter::SerializeGraph(BlendStackGraph)));
	}

	if (const FObjectPropertyBase* InputActionProperty = FindFProperty<FObjectPropertyBase>(Node->GetClass(), FName(TEXT("InputAction"))))
	{
		if (UObject* InputActionObject = InputActionProperty->GetObjectPropertyValue_InContainer(Node))
		{
			Info.NodeProps.Add(TEXT("InputAction"), InputActionObject->GetPathName());
		}
	}

	const FString NodeClassName = Node->GetClass()->GetName();
	if (NodeClassName == TEXT("K2Node_PropertyAccess"))
	{
		AddNodePropertyTextIfPresent_ExportBpy(Node, Info, TEXT("Path"));
		AddNodePropertyTextIfPresent_ExportBpy(Node, Info, TEXT("ContextId"));
	}
	else if (NodeClassName == TEXT("K2Node_AnimNodeReference"))
	{
		AddNodePropertyTextIfPresent_ExportBpy(Node, Info, TEXT("Tag"));
	}
	else if (NodeClassName == TEXT("K2Node_GetArrayItem"))
	{
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("bReturnByRefDesired"));
	}
	else if (const UK2Node_InputKey* InputKeyNode = Cast<UK2Node_InputKey>(Node))
	{
		Info.NodeProps.Add(TEXT("InputKey"), InputKeyNode->InputKey.ToString());
		Info.NodeProps.Add(TEXT("bConsumeInput"), InputKeyNode->bConsumeInput ? TEXT("true") : TEXT("false"));
		Info.NodeProps.Add(TEXT("bExecuteWhenPaused"), InputKeyNode->bExecuteWhenPaused ? TEXT("true") : TEXT("false"));
		Info.NodeProps.Add(TEXT("bOverrideParentBinding"), InputKeyNode->bOverrideParentBinding ? TEXT("true") : TEXT("false"));
		Info.NodeProps.Add(TEXT("bControl"), InputKeyNode->bControl ? TEXT("true") : TEXT("false"));
		Info.NodeProps.Add(TEXT("bAlt"), InputKeyNode->bAlt ? TEXT("true") : TEXT("false"));
		Info.NodeProps.Add(TEXT("bShift"), InputKeyNode->bShift ? TEXT("true") : TEXT("false"));
		Info.NodeProps.Add(TEXT("bCommand"), InputKeyNode->bCommand ? TEXT("true") : TEXT("false"));
	}

	if (NodeClassName == TEXT("K2Node_EvaluateChooser") || NodeClassName == TEXT("K2Node_EvaluateChooser2"))
	{
		// Chooser node dynamic pins are derived from Chooser/Mode at import time.
		// Export a non-delta fallback to avoid silently dropping these properties.
		AddChooserPropertyTextIfPresent_ExportBpy(Node, Info);
		AddNodePropertyDeltaOrTextIfPresent_ExportBpy(Node, Info, TEXT("Mode"));
		AddNodePropertyDeltaOrTextIfPresent_ExportBpy(Node, Info, TEXT("bReturnSoftObjectReference"));
	}
	else if (NodeClassName == TEXT("K2Node_EvaluateProxy") || NodeClassName == TEXT("K2Node_EvaluateProxy2"))
	{
		AddNodeObjectPropertyTextIfPresent_ExportBpy(Node, Info, TEXT("Proxy"));
		AddNodePropertyDeltaOrTextIfPresent_ExportBpy(Node, Info, TEXT("Mode"));
	}

	TSet<FString> AnimBindingDrivenPins;
	if (Info.NodeType.StartsWith(TEXT("AnimGraphNode_")))
	{
		AnimBindingDrivenPins = CollectAnimNodeBindingDrivenPinNames_ExportBpy(Node);
	}
	const bool bTraceAnimBindingNode = ShouldTraceAnimBindingNode_ExportBpy(Node);

	for (UEdGraphPin* Pin : Node->Pins)
	{
		const FString RawPinName = Pin->PinName.ToString();
		const FString LogicalPinName = GetLogicalPinName_ExportBpy(Node, Pin);
		const FString PinNameForMetadata = LogicalPinName.IsEmpty() ? RawPinName : LogicalPinName;
		const FString PinTypeString = NormalizeTypeString_ExportBpy(Pin->PinType);
		if (Pin->Direction == EGPD_Input)
		{
			Info.InputPinTypes.Add(PinNameForMetadata, PinTypeString);
		}
		else if (Pin->Direction == EGPD_Output)
		{
			Info.OutputPinTypes.Add(PinNameForMetadata, PinTypeString);
		}

		if (const UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
		{
			const bool bIsExecPin = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
			const bool bIsDelegatePin = RawPinName == UK2Node_Event::DelegateOutputName.ToString();
			if (Pin->Direction == EGPD_Output && !bIsExecPin && !bIsDelegatePin)
			{
				Info.CustomParams.Add(
					TPair<FString, FString>(
						LogicalPinName.IsEmpty() ? RawPinName : LogicalPinName,
						NormalizeTypeString_ExportBpy(Pin->PinType)));
			}
		}

		if (!LogicalPinName.IsEmpty())
		{
			Info.PinIds.Add(LogicalPinName, Pin->PinId.ToString(EGuidFormats::Digits));
			if (LogicalPinName != RawPinName)
			{
				Info.PinAliases.Add(LogicalPinName, RawPinName);
			}
		}

		const bool bIsAnimBindingDrivenInput =
			Pin &&
			Pin->Direction == EGPD_Input &&
			(AnimBindingDrivenPins.Contains(RawPinName) ||
				(!LogicalPinName.IsEmpty() && AnimBindingDrivenPins.Contains(LogicalPinName)));

		if (Pin && Pin->Direction == EGPD_Input)
		{
			const FString PinDefaultValue = GetPinDefaultValueForExport_ExportBpy(Pin);

			if (bTraceAnimBindingNode)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[ExportBpy][BindingTrace] node=%s class=%s raw_pin=%s logical_pin=%s metadata_pin=%s default=%s is_binding=%s has_raw=%s has_logical=%s"),
					*Node->GetName(),
					*Info.NodeType,
					*RawPinName,
					*LogicalPinName,
					*PinNameForMetadata,
					*PinDefaultValue,
					bIsAnimBindingDrivenInput ? TEXT("true") : TEXT("false"),
					AnimBindingDrivenPins.Contains(RawPinName) ? TEXT("true") : TEXT("false"),
					(!LogicalPinName.IsEmpty() && AnimBindingDrivenPins.Contains(LogicalPinName)) ? TEXT("true") : TEXT("false"));
			}

			if (!bIsAnimBindingDrivenInput && !PinDefaultValue.IsEmpty())
			{
				Info.DefaultValues.Add(PinNameForMetadata, PinDefaultValue);
			}
		}
	}

	if (Info.NodeType.StartsWith(TEXT("AnimGraphNode_")))
	{
		if (Info.NodeType == TEXT("AnimGraphNode_TwoWayBlend"))
		{
			AddNodePropertyTextIfPresent_ExportBpy(Node, Info, TEXT("Node"));
		}
		else
		{
			AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("Node"));
		}
		AddNodePropertyTextIfPresent_ExportBpy(Node, Info, TEXT("ShowPinForProperties"));
		AddNodePropertyTextIfPresent_ExportBpy(Node, Info, TEXT("CustomPinProperties"));
		AddAnimNodeBindingPropertyBindingsIfPresent_ExportBpy(Node, Info);
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("InitialUpdateFunction"));
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("BecomeRelevantFunction"));
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("UpdateFunction"));
		AddNodePropertyDeltaTextIfPresent_ExportBpy(Node, Info, TEXT("OnMotionMatchingStateUpdatedFunction"));
		if (UAnimGraphNode_StateResult* StateResultNode = Cast<UAnimGraphNode_StateResult>(Node))
		{
			TSharedPtr<FJsonObject> StateResultProps = MakeShared<FJsonObject>();
			AddGenericNodePropertyText_ExportBpy(StateResultNode, StateResultProps, TEXT("StateEntryFunction"), true);
			AddGenericNodePropertyText_ExportBpy(StateResultNode, StateResultProps, TEXT("StateFullyBlendedInFunction"), true);
			AddGenericNodePropertyText_ExportBpy(StateResultNode, StateResultProps, TEXT("StateExitFunction"), true);
			AddGenericNodePropertyText_ExportBpy(StateResultNode, StateResultProps, TEXT("StateFullyBlendedOutFunction"), true);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : StateResultProps->Values)
			{
				if (Entry.Value.IsValid() && Entry.Value->Type == EJson::String)
				{
					Info.NodeProps.Add(Entry.Key, Entry.Value->AsString());
				}
			}
			NormalizeStateResultRuntimeNodeTextFromEditorRefs_ExportBpy(Info);
		}
		if (const UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
		{
			Info.NodeProps.Add(TEXT("LinkedAnimLayerLayer"), LinkedLayerNode->Node.Layer.ToString());
			if (UClass* InterfaceClass = ResolveLinkedAnimLayerInterfaceClass_ExportBpy(LinkedLayerNode))
			{
				Info.NodeProps.Add(TEXT("LinkedAnimLayerInterfaceClass"), InterfaceClass->GetPathName());
			}
			else
			{
				Info.NodeProps.Add(TEXT("LinkedAnimLayerInterfaceClass"), TEXT("None"));
			}
		}

		// Strip bound fields from the serialized `Node=(...)` struct so that
		// re-import doesn't overwrite the PropertyBindings-driven values with
		// stale in-memory constants cached inside the FAnimNode_* struct.
		if (AnimBindingDrivenPins.Num() > 0)
		{
			if (FString* NodeStruct = Info.NodeProps.Find(TEXT("Node")))
			{
				StripBoundFieldsFromStructText_ExportBpy(*NodeStruct, AnimBindingDrivenPins);
				if (NodeStruct->IsEmpty() || *NodeStruct == TEXT("()"))
				{
					Info.NodeProps.Remove(TEXT("Node"));
				}
			}
		}
	}

	return Info;
}

FNodeInfo BuildNodeInfoForGenericGraphNode_ExportBpy(UEdGraphNode* Node)
{
	FNodeInfo Info;
	if (!Node)
	{
		return Info;
	}

	Info.NodeType = Node->GetClass() ? Node->GetClass()->GetName() : TEXT("UnknownNode");
	Info.Position = FVector2D(Node->NodePosX, Node->NodePosY);
	Info.NodeGuid = Node->NodeGuid.ToString(EGuidFormats::Digits);

	auto AddNodeProp = [&Info, Node](const TCHAR* PropertyName, bool bUseDelta = false)
	{
		if (!PropertyName || !Node || !Node->GetClass())
		{
			return;
		}

		FProperty* Property = Node->GetClass()->FindPropertyByName(FName(PropertyName));
		if (!Property)
		{
			return;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
		if (!ValuePtr)
		{
			return;
		}

		void* DefaultPtr = nullptr;
		if (bUseDelta)
		{
			if (UObject* DefaultObject = Node->GetClass()->GetDefaultObject())
			{
				DefaultPtr = Property->ContainerPtrToValuePtr<void>(DefaultObject);
			}
		}

		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, ValuePtr, DefaultPtr, Node, PPF_None);
		if (!ExportedValue.IsEmpty())
		{
			Info.NodeProps.Add(PropertyName, ExportedValue);
		}
	};

	if (Node->IsA<UEdGraphNode_Comment>())
	{
		AddNodeProp(TEXT("NodeComment"));
		AddNodeProp(TEXT("CommentColor"));
		AddNodeProp(TEXT("FontSize"));
		AddNodeProp(TEXT("MoveMode"));
		AddNodeProp(TEXT("CommentDepth"));
		AddNodeProp(TEXT("bColorCommentBubble"));
		AddNodeProp(TEXT("bCommentBubbleVisible"));
		AddNodeProp(TEXT("NodeWidth"));
		AddNodeProp(TEXT("NodeHeight"));
	}

	return Info;
}

void AppendStringMapSection_ExportBpy(
	FString& InOut,
	const FString& SectionName,
	const TMap<FString, FString>& Values,
	bool bTrailingComma = true)
{
	InOut += FString::Printf(TEXT("    %s: {\n"), *MakePythonStringLiteral_ExportBpy(SectionName));

	TArray<FString> Keys;
	Values.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		const FString* Value = Values.Find(Key);
		if (!Value)
		{
			continue;
		}

		InOut += FString::Printf(
			TEXT("        %s: %s,\n"),
			*MakePythonStringLiteral_ExportBpy(Key),
			*MakePythonStringLiteral_ExportBpy(*Value));
	}

	InOut += TEXT("    }");
	if (bTrailingComma)
	{
		InOut += TEXT(",\n\n");
	}
	else
	{
		InOut += TEXT("\n");
	}
}

void AppendVectorMapSection_ExportBpy(
	FString& InOut,
	const FString& SectionName,
	const TMap<FString, FVector2D>& Values,
	bool bTrailingComma = true)
{
	InOut += FString::Printf(TEXT("    %s: {\n"), *MakePythonStringLiteral_ExportBpy(SectionName));

	TArray<FString> Keys;
	Values.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		const FVector2D* Value = Values.Find(Key);
		if (!Value)
		{
			continue;
		}

		InOut += FString::Printf(
			TEXT("        %s: (%d, %d),\n"),
			*MakePythonStringLiteral_ExportBpy(Key),
			(int32)Value->X,
			(int32)Value->Y);
	}

	InOut += TEXT("    }");
	if (bTrailingComma)
	{
		InOut += TEXT(",\n\n");
	}
	else
	{
		InOut += TEXT("\n");
	}
}

void AppendNestedMapSection_ExportBpy(
	FString& InOut,
	const FString& SectionName,
	const TMap<FString, TMap<FString, FString>>& Values,
	bool bTrailingComma = false)
{
	InOut += FString::Printf(TEXT("    %s: {\n"), *MakePythonStringLiteral_ExportBpy(SectionName));

	TArray<FString> OuterKeys;
	Values.GetKeys(OuterKeys);
	OuterKeys.Sort();
	for (const FString& OuterKey : OuterKeys)
	{
		const TMap<FString, FString>* InnerMap = Values.Find(OuterKey);
		if (!InnerMap)
		{
			continue;
		}

		InOut += FString::Printf(TEXT("        %s: {\n"), *MakePythonStringLiteral_ExportBpy(OuterKey));

		TArray<FString> InnerKeys;
		InnerMap->GetKeys(InnerKeys);
		InnerKeys.Sort();
		for (const FString& InnerKey : InnerKeys)
		{
			const FString* InnerValue = InnerMap->Find(InnerKey);
			if (!InnerValue)
			{
				continue;
			}

			InOut += FString::Printf(
				TEXT("            %s: %s,\n"),
				*MakePythonStringLiteral_ExportBpy(InnerKey),
				*MakePythonStringLiteral_ExportBpy(*InnerValue));
		}

		InOut += TEXT("        },\n");
	}

	InOut += TEXT("    }");
	if (bTrailingComma)
	{
		InOut += TEXT(",\n\n");
	}
	else
	{
		InOut += TEXT("\n");
	}
}

bool TryParseJsonObjectString_ExportBpy(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
{
	OutObject.Reset();
	if (JsonText.IsEmpty())
	{
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
	{
		return false;
	}

	return true;
}

void CollectAnimExportContractViolationsFromGraph_ExportBpy(
	const TSharedPtr<FJsonObject>& GraphObj,
	const FString& GraphPath,
	TArray<FString>& OutViolations)
{
	if (!GraphObj.IsValid())
	{
		return;
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

		FString NodeClass;
		NodeObj->TryGetStringField(TEXT("node_class"), NodeClass);

		FString NodeGuid;
		NodeObj->TryGetStringField(TEXT("uid"), NodeGuid);
		if (NodeGuid.IsEmpty())
		{
			NodeObj->TryGetStringField(TEXT("node_guid"), NodeGuid);
		}

		const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
		const bool bHasNodeProps =
			NodeObj->TryGetObjectField(TEXT("node_props"), NodePropsObj) &&
			NodePropsObj &&
			NodePropsObj->IsValid();

		if (NodeClass.Equals(TEXT("AnimGraphNode_LinkedAnimLayer"), ESearchCase::CaseSensitive) && bHasNodeProps)
		{
			FString LayerName;
			(*NodePropsObj)->TryGetStringField(TEXT("LinkedAnimLayerLayer"), LayerName);

			FString InterfaceClassPath;
			(*NodePropsObj)->TryGetStringField(TEXT("LinkedAnimLayerInterfaceClass"), InterfaceClassPath);
			FString NodeStructText;
			(*NodePropsObj)->TryGetStringField(TEXT("Node"), NodeStructText);
			const bool bNodeStructHasExplicitInterface =
				NodeStructText.Contains(TEXT("Interface="), ESearchCase::CaseSensitive);

			if (!LayerName.IsEmpty() &&
				bNodeStructHasExplicitInterface &&
				(InterfaceClassPath.IsEmpty() || InterfaceClassPath.Equals(TEXT("None"), ESearchCase::IgnoreCase)))
			{
				OutViolations.Add(FString::Printf(
					TEXT("linked_layer_interface_missing graph=%s node=%s layer=%s"),
					*GraphPath,
					*NodeGuid,
					*LayerName));
			}
		}

		if (NodeClass.Equals(TEXT("AnimStateNode"), ESearchCase::CaseSensitive) && bHasNodeProps)
		{
			FString StateEnteredText;
			(*NodePropsObj)->TryGetStringField(TEXT("StateEntered"), StateEnteredText);
			const FString ActualNotifyName =
				ExtractStateNotifyNameFromStateEnteredText_ExportBpy(StateEnteredText);

			FString BoundGraphJson;
			(*NodePropsObj)->TryGetStringField(TEXT("BoundGraphJson"), BoundGraphJson);
			TSharedPtr<FJsonObject> BoundGraphObj;
			if (TryParseJsonObjectString_ExportBpy(BoundGraphJson, BoundGraphObj))
			{
				FString ExpectedNotifyName;
				const TArray<TSharedPtr<FJsonValue>>* BoundNodesArr = nullptr;
				if (BoundGraphObj->TryGetArrayField(TEXT("nodes"), BoundNodesArr) && BoundNodesArr)
				{
					for (const TSharedPtr<FJsonValue>& BoundNodeValue : *BoundNodesArr)
					{
						const TSharedPtr<FJsonObject> BoundNodeObj =
							BoundNodeValue.IsValid() ? BoundNodeValue->AsObject() : nullptr;
						if (!BoundNodeObj.IsValid())
						{
							continue;
						}

						FString BoundNodeClass;
						BoundNodeObj->TryGetStringField(TEXT("node_class"), BoundNodeClass);
						if (!BoundNodeClass.Equals(TEXT("AnimGraphNode_StateResult"), ESearchCase::CaseSensitive))
						{
							continue;
						}

						const TSharedPtr<FJsonObject>* BoundNodePropsObj = nullptr;
						if (!BoundNodeObj->TryGetObjectField(TEXT("node_props"), BoundNodePropsObj) ||
							!BoundNodePropsObj ||
							!BoundNodePropsObj->IsValid())
						{
							continue;
						}

						FString StateResultNodeText;
						(*BoundNodePropsObj)->TryGetStringField(TEXT("Node"), StateResultNodeText);
						ExpectedNotifyName = ExtractStateResultHookFunctionName_ExportBpy(
							StateResultNodeText,
							TEXT("StateEntryFunction"));
						if (!ExpectedNotifyName.IsEmpty())
						{
							break;
						}
					}
				}

				if (!ExpectedNotifyName.IsEmpty() && ActualNotifyName.IsEmpty())
				{
					OutViolations.Add(FString::Printf(
						TEXT("state_entry_binding_missing graph=%s node=%s expected=%s actual=%s"),
						*GraphPath,
						*NodeGuid,
						*ExpectedNotifyName,
						*ActualNotifyName));
				}
			}
		}

		if (bHasNodeProps)
		{
			for (const TCHAR* NestedField : NestedGraphFields)
			{
				FString NestedJson;
				if (!(*NodePropsObj)->TryGetStringField(NestedField, NestedJson) || NestedJson.IsEmpty())
				{
					continue;
				}

				TSharedPtr<FJsonObject> NestedGraphObj;
				if (!TryParseJsonObjectString_ExportBpy(NestedJson, NestedGraphObj))
				{
					OutViolations.Add(FString::Printf(
						TEXT("nested_graph_json_parse_failed graph=%s node=%s field=%s"),
						*GraphPath,
						*NodeGuid,
						NestedField));
					continue;
				}

				CollectAnimExportContractViolationsFromGraph_ExportBpy(
					NestedGraphObj,
					GraphPath + TEXT("::") + NodeGuid + TEXT(".") + NestedField,
					OutViolations);
			}
		}
	}
}

bool ValidatePostExportContracts_ExportBpy(UBlueprint* BP, FString& OutError)
{
	OutError.Reset();
	if (!BP || !BP->IsA<UAnimBlueprint>())
	{
		return true;
	}

	const TSharedPtr<FJsonObject> Root = UBPDirectExporter::SerializeBlueprintToJson(BP);
	if (!Root.IsValid())
	{
		OutError = TEXT("Post-export validation failed: cannot serialize blueprint to JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* GraphsArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("graphs"), GraphsArr) || !GraphsArr)
	{
		OutError = TEXT("Post-export validation failed: missing graphs array");
		return false;
	}

	TArray<FString> Violations;
	for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArr)
	{
		const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
		if (!GraphObj.IsValid())
		{
			continue;
		}

		FString GraphName;
		GraphObj->TryGetStringField(TEXT("name"), GraphName);
		if (GraphName.IsEmpty())
		{
			GraphName = TEXT("<unnamed_graph>");
		}

		CollectAnimExportContractViolationsFromGraph_ExportBpy(GraphObj, GraphName, Violations);
	}

	if (Violations.Num() > 0)
	{
		OutError = FString::Printf(
			TEXT("Post-export contract validation failed: %s"),
			*FString::Join(Violations, TEXT("; ")));
		return false;
	}

	return true;
}
}

// ─── public entry point ───────────────────────────────────────────────────────

bool UBPDirectExporter::ExportBlueprintToPy(
	const FString& BlueprintPath,
	const FString& OutputDir,
	FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!BP)
	{
		BP = Cast<UBlueprint>(StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, *BlueprintPath));
	}
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Cannot load blueprint: %s"), *BlueprintPath);
		return false;
	}

	if (!ValidatePostExportContracts_ExportBpy(BP, OutError))
	{
		return false;
	}

	const FString BPName = FPaths::GetBaseFilename(BlueprintPath);
	const FString BPOutDir = FPaths::Combine(OutputDir, BPName);
	IFileManager::Get().MakeDirectory(*BPOutDir, true);
	{
		TArray<FString> ExistingSidecarFiles;
		IFileManager::Get().FindFiles(ExistingSidecarFiles, *(FPaths::Combine(BPOutDir, TEXT("other_*.bp.py"))), true, false);
		for (const FString& FileName : ExistingSidecarFiles)
		{
			IFileManager::Get().Delete(*(FPaths::Combine(BPOutDir, FileName)), false, true);
		}
	}

	auto AppendConnectionSections = [&](const TArray<UK2Node*>& SourceNodes, const TArray<FNodeInfo>& NodeInfos, const TMap<UK2Node*, FString>& NodeVarMap, FString& InOutLines)
	{
		TArray<FString> DataLines;
		TArray<FString> ExecLines;
		TSet<FString> SeenConnections;

		auto TryAppendConnection = [&](UK2Node* SrcNode, const FString& SrcVar, UEdGraphPin* SrcPin, UEdGraphPin* RawDst)
		{
			TArray<UEdGraphPin*> DestinationPins;
			const bool bSrcIsKnot = SrcNode && SrcNode->IsA<UK2Node_Knot>();
			const bool bDstIsKnot = RawDst && RawDst->GetOwningNode() && RawDst->GetOwningNode()->IsA<UK2Node_Knot>();
			if (bSrcIsKnot || bDstIsKnot)
			{
				if (RawDst)
				{
					DestinationPins.Add(RawDst);
				}
			}
			else
			{
				ResolveRerouteChainAll(RawDst, DestinationPins);
			}

			for (UEdGraphPin* DstPin : DestinationPins)
			{
				UK2Node* DstNode = Cast<UK2Node>(DstPin->GetOwningNode());
				if (!DstNode)
				{
					continue;
				}

				const FString* DstVar = NodeVarMap.Find(DstNode);
				if (!DstVar)
				{
					continue;
				}

				const FString SrcRef = TranslateOutputPinRef_ExportBpy(SrcNode, SrcVar, SrcPin);
				const FString DstRef = TranslateInputPinRef_ExportBpy(DstNode, *DstVar, DstPin);
				const FString ConnLine = SrcRef + TEXT(" >> ") + DstRef;
				if (SeenConnections.Contains(ConnLine))
				{
					continue;
				}

				SeenConnections.Add(ConnLine);
				if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					ExecLines.Add(ConnLine);
				}
				else
				{
					DataLines.Add(ConnLine);
				}
			}
		};

		for (int32 Index = 0; Index < SourceNodes.Num(); ++Index)
		{
			UK2Node* SrcNode = SourceNodes[Index];
			const FString& SrcVar = NodeInfos[Index].VarName;
			for (UEdGraphPin* SrcPin : SrcNode->Pins)
			{
				if (SrcPin->Direction != EGPD_Output)
				{
					continue;
				}

				for (UEdGraphPin* RawDst : SrcPin->LinkedTo)
				{
					TryAppendConnection(SrcNode, SrcVar, SrcPin, RawDst);
				}

				if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					for (UEdGraphPin* SyntheticDst : GetSyntheticExecPassThroughTargets_ExportBpy(SrcNode, SrcPin))
					{
						TryAppendConnection(SrcNode, SrcVar, SrcPin, SyntheticDst);
					}
				}
			}
		}

		if (!DataLines.IsEmpty())
		{
			InOutLines += TEXT("        # Data flow\n");
			for (const FString& Line : DataLines)
			{
				InOutLines += TEXT("        ") + Line + TEXT("\n");
			}
			InOutLines += TEXT("\n");
		}

		if (!ExecLines.IsEmpty())
		{
			InOutLines += TEXT("        # Exec flow\n");
			for (const FString& Line : ExecLines)
			{
				InOutLines += TEXT("        ") + Line + TEXT("\n");
			}
			InOutLines += TEXT("\n");
		}
	};

	TArray<FString> GraphModules;
	TArray<UEdGraph*> RootGraphs;
	CollectRootGraphs_ExportBpy(BP, RootGraphs);
	for (UEdGraph* Graph : RootGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		FString ModuleName;
		if (!GenerateGraphFile(BP, Graph, GetRootGraphPrefix_ExportBpy(BP, Graph), BPOutDir, ModuleName, OutError))
		{
			return false;
		}

		GraphModules.Add(ModuleName);
	}

	if (!GenerateMainFile(BP, BPOutDir, GraphModules, OutError))
	{
		return false;
	}

	// Keep the adjacent single-file export in sync with the package export.
	FString BpyText;
	FString BpyError;
	if (!ReadBlueprintToBpyText(BlueprintPath, BpyText, BpyError))
	{
		OutError = BpyError.IsEmpty()
			? FString::Printf(TEXT("Failed to generate companion bpy file for %s"), *BlueprintPath)
			: BpyError;
		return false;
	}

	const FString CompanionBpyPath = FPaths::Combine(BPOutDir, BPName + TEXT(".bp.py"));
	if (!FFileHelper::SaveStringToFile(BpyText, *CompanionBpyPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Cannot write %s"), *CompanionBpyPath);
		return false;
	}

	return true;
}

bool UBPDirectExporter::ReadBlueprintToBpyText(
	const FString& BlueprintPath,
	FString& OutBpyText,
	FString& OutError)
{
	OutBpyText.Reset();
	OutError.Reset();

	UBlueprint* BP = LoadBlueprintAsset_ExportBpy(BlueprintPath, OutError);
	if (!BP)
	{
		return false;
	}

	if (!ValidatePostExportContracts_ExportBpy(BP, OutError))
	{
		return false;
	}

	TArray<FString> GraphModules;
	CollectGraphModuleNames_ExportBpy(BP, GraphModules);

	FString ParentClass = TEXT("/Script/Engine.Actor");
	if (BP->ParentClass)
	{
		ParentClass = BP->ParentClass->GetPathName();
	}

	const FString BlueprintAssetPath = BP->GetPathName();
	const FString BlueprintType = BP->IsA<UAnimBlueprint>() ? TEXT("AnimBlueprint") : TEXT("Normal");

	FString Content;
	Content += TEXT("# Auto-generated by ExportBpy\n");
	Content += TEXT("import importlib.util\n");
	Content += TEXT("import os\n");
	Content += TEXT("from ue_bp_dsl import Blueprint\n");
	Content += TEXT("\n");
	Content += TEXT("def _load_graph_module(stem):\n");
	Content += TEXT("    file_path = os.path.join(os.path.dirname(__file__), f\"{stem}.bp.py\")\n");
	Content += TEXT("    spec = importlib.util.spec_from_file_location(f\"_exportbpy_graph_{stem}\", file_path)\n");
	Content += TEXT("    if spec is None or spec.loader is None:\n");
	Content += TEXT("        raise ImportError(f\"Cannot load graph module: {file_path}\")\n");
	Content += TEXT("    module = importlib.util.module_from_spec(spec)\n");
	Content += TEXT("    spec.loader.exec_module(module)\n");
	Content += TEXT("    return module\n\n");
	Content += TEXT("_GRAPH_MODULES = [\n");
	for (const FString& ModuleName : GraphModules)
	{
		Content += FString::Printf(TEXT("    _load_graph_module(%s),\n"), *MakePythonStringLiteral_ExportBpy(ModuleName));
	}
	Content += TEXT("]\n\n");

	Content += FString::Printf(
		TEXT("bp = Blueprint(\n    path=%s,\n    parent=%s,\n    bp_type=%s,\n)\n\n"),
		*MakePythonStringLiteral_ExportBpy(BlueprintAssetPath),
		*MakePythonStringLiteral_ExportBpy(ParentClass),
		*MakePythonStringLiteral_ExportBpy(BlueprintType));

	Content += GenerateVariablesSection(BP);
	Content += GenerateClassDefaultsSection(BP);
	Content += GenerateInheritedComponentDefaultsSection(BP);
	Content += GenerateComponentsSection(BP);
	Content += GenerateInterfacesSection(BP);
	Content += GenerateDispatchersSection(BP);
	Content += TEXT("bp.build()\n");
	Content += TEXT("for _graph_module in _GRAPH_MODULES:\n");
	Content += TEXT("    _graph_module.register(bp)\n");

	OutBpyText = MoveTemp(Content);
	return true;
}

bool UBPDirectExporter::ExportBlueprintToBpyFile(
	const FString& BlueprintPath,
	const FString& OutputPath,
	FString& OutError)
{
	OutError.Reset();

	FString BpyText;
	if (!ReadBlueprintToBpyText(BlueprintPath, BpyText, OutError))
	{
		return false;
	}

	UBlueprint* BP = LoadBlueprintAsset_ExportBpy(BlueprintPath, OutError);
	if (!BP)
	{
		return false;
	}

	FString FinalOutputPath = OutputPath;
	FinalOutputPath.TrimStartAndEndInline();
	if (FinalOutputPath.IsEmpty())
	{
		FinalOutputPath = BuildDefaultBpyExportPath_ExportBpy(BP);
	}
	else
	{
		if (FPaths::IsRelative(FinalOutputPath))
		{
			FinalOutputPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FinalOutputPath);
		}

		if (FPaths::GetExtension(FinalOutputPath).IsEmpty())
		{
			FinalOutputPath = FPaths::Combine(FinalOutputPath, BP->GetName() + TEXT(".bp.py"));
		}
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FinalOutputPath), true);
	if (!FFileHelper::SaveStringToFile(
			BpyText,
			*FinalOutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Cannot write %s"), *FinalOutputPath);
		return false;
	}

	return true;
}

// ─── GenerateMainFile ─────────────────────────────────────────────────────────

bool UBPDirectExporter::GenerateMainFile(
	UBlueprint* BP,
	const FString& OutDir,
	const TArray<FString>& GraphModules,
	FString& OutError)
{
	FString Content;
	Content += TEXT("# Auto-generated by ExportBpy\n");
	Content += TEXT("import importlib.util\n");
	Content += TEXT("import os\n");
	Content += TEXT("from ue_bp_dsl import Blueprint\n");
	Content += TEXT("\n");
	Content += TEXT("def _load_graph_module(stem):\n");
	Content += TEXT("    file_path = os.path.join(os.path.dirname(__file__), f\"{stem}.bp.py\")\n");
	Content += TEXT("    spec = importlib.util.spec_from_file_location(f\"_exportbpy_graph_{stem}\", file_path)\n");
	Content += TEXT("    if spec is None or spec.loader is None:\n");
	Content += TEXT("        raise ImportError(f\"Cannot load graph module: {file_path}\")\n");
	Content += TEXT("    module = importlib.util.module_from_spec(spec)\n");
	Content += TEXT("    spec.loader.exec_module(module)\n");
	Content += TEXT("    return module\n\n");
	Content += TEXT("_GRAPH_MODULES = [\n");
	for (const FString& ModuleName : GraphModules)
	{
		Content += FString::Printf(TEXT("    _load_graph_module(%s),\n"), *MakePythonStringLiteral_ExportBpy(ModuleName));
	}
	Content += TEXT("]\n\n");

	FString ParentClass = TEXT("/Script/Engine.Actor");
	if (BP->ParentClass)
	{
		ParentClass = BP->ParentClass->GetPathName();
	}

	const FString BlueprintPath = BP->GetPathName();
	const FString BPType = BP->IsA<UAnimBlueprint>() ? TEXT("AnimBlueprint") : TEXT("Normal");

	Content += FString::Printf(
		TEXT("bp = Blueprint(\n    path=%s,\n    parent=%s,\n    bp_type=%s,\n)\n\n"),
		*MakePythonStringLiteral_ExportBpy(BlueprintPath),
		*MakePythonStringLiteral_ExportBpy(ParentClass),
		*MakePythonStringLiteral_ExportBpy(BPType));

	Content += GenerateVariablesSection(BP);
	Content += GenerateClassDefaultsSection(BP);
	Content += GenerateInheritedComponentDefaultsSection(BP);
	Content += GenerateComponentsSection(BP);
	Content += GenerateInterfacesSection(BP);
	Content += GenerateDispatchersSection(BP);
	Content += TEXT("bp.build()\n");
	Content += TEXT("for _graph_module in _GRAPH_MODULES:\n");
	Content += TEXT("    _graph_module.register(bp)\n");
	Content += TEXT("\n");

	FString OutPath = FPaths::Combine(OutDir, TEXT("__bp__.bp.py"));
	if (!FFileHelper::SaveStringToFile(Content, *OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Cannot write %s"), *OutPath);
		return false;
	}
	return true;
}

// ─── GenerateVariablesSection ─────────────────────────────────────────────────

FString UBPDirectExporter::GenerateVariablesSection(UBlueprint* BP)
{
	FString Out;
	Out += TEXT("# ── Variables ───────────────────────────────────────────\n");

	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		// Skip delegate variables — handled in dispatchers section
		if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate ||
			Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
			continue;

		const FString TypeStr = NormalizeTypeString_ExportBpy(Var.VarType);
		const FString DefaultValue = GetBlueprintVariableDefaultValue_ExportBpy(BP, Var);

		FString ContainerStr = TEXT("single");
		if (Var.VarType.IsArray()) ContainerStr = TEXT("array");
		else if (Var.VarType.IsSet()) ContainerStr = TEXT("set");
		else if (Var.VarType.IsMap()) ContainerStr = TEXT("map");

		FString VariableLine = FString::Printf(
			TEXT("bp.var(%s, %s, container=%s, default=%s"),
			*MakePythonStringLiteral_ExportBpy(Var.VarName.ToString()),
			*MakePythonStringLiteral_ExportBpy(TypeStr),
			*MakePythonStringLiteral_ExportBpy(ContainerStr),
			*MakePythonStringLiteral_ExportBpy(DefaultValue));
		if (Var.VarGuid.IsValid())
		{
			VariableLine += FString::Printf(
				TEXT(", guid=%s"),
				*MakePythonStringLiteral_ExportBpy(Var.VarGuid.ToString(EGuidFormats::Digits)));
		}
		const FString TooltipValue = Var.MetaDataArray.IsEmpty() ? TEXT("") : Var.MetaDataArray[0].DataValue;
		VariableLine += FString::Printf(
			TEXT(", category=%s, replicated=%s, rep_notify=%s, instance_editable=%s, tooltip=%s"),
			*MakePythonStringLiteral_ExportBpy(Var.Category.ToString()),
			(Var.PropertyFlags & CPF_Net) != 0 ? TEXT("True") : TEXT("False"),
			*MakePythonStringLiteral_ExportBpy(Var.RepNotifyFunc.ToString()),
			(Var.PropertyFlags & CPF_Edit) != 0 ? TEXT("True") : TEXT("False"),
			*MakePythonStringLiteral_ExportBpy(TooltipValue));
		VariableLine += TEXT(")\n");
		Out += VariableLine;
	}
	Out += TEXT("\n");
	return Out;
}

// ─── GenerateClassDefaultsSection ───────────────────────────────────────────

FString UBPDirectExporter::GenerateClassDefaultsSection(UBlueprint* BP)
{
	FString Out;
	Out += TEXT("# ── Class Defaults ──────────────────────────────────────\n");

	if (!BP->GeneratedClass)
	{
		Out += TEXT("\n");
		return Out;
	}

	UObject* BPCDO = BP->GeneratedClass->GetDefaultObject(false);
	UClass* SuperClass = BP->GeneratedClass->GetSuperClass();
	UObject* ParentCDO = (SuperClass && SuperClass->GetDefaultObject(false))
		? SuperClass->GetDefaultObject(false) : nullptr;

	if (!BPCDO || !ParentCDO)
	{
		Out += TEXT("\n");
		return Out;
	}

	bool bHadAny = false;

	for (TFieldIterator<FProperty> It(BP->GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Prop = *It;
		if (!Prop) continue;

		// Skip non-designer-visible properties
		if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly))
			continue;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_Deprecated))
			continue;

		// Skip collections — importer doesn't handle them
		if (CastField<FArrayProperty>(Prop) || CastField<FSetProperty>(Prop) || CastField<FMapProperty>(Prop))
			continue;

		const void* BPPtr     = Prop->ContainerPtrToValuePtr<void>(BPCDO);

		// Only delta_scroll against parent CDO if parent class owns this property
		const UClass* PropOwnerClass = Prop->GetOwnerClass();
		const void* ParentPtr = (PropOwnerClass && ParentCDO->GetClass()->IsChildOf(PropOwnerClass))
			? Prop->ContainerPtrToValuePtr<void>(ParentCDO) : nullptr;

		// If parent doesn't have this property, skip (it's a BP-only variable, handled elsewhere)
		if (!BPPtr || !ParentPtr) continue;

		// Skip if identical to parent CDO
		if (Prop->Identical(BPPtr, ParentPtr)) continue;

		// Skip object properties that are DefaultSubobjects (component slot pointers)
		if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* ObjVal = ObjProp->GetObjectPropertyValue(BPPtr);
			if (!ObjVal || ObjVal->IsDefaultSubobject()) continue;
		}

		// Serialize value — same dispatch chain as BuildComponentPropertiesPyDict_ExportBpy
		FString PyValue;

		if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* ObjVal = ObjProp->GetObjectPropertyValue(BPPtr);
			if (!ObjVal) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(ObjVal->GetPathName());
		}
		else if (const FClassProperty* ClsProp = CastField<FClassProperty>(Prop))
		{
			UClass* ClsVal = Cast<UClass>(ClsProp->GetObjectPropertyValue(BPPtr));
			if (!ClsVal) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(ClsVal->GetPathName());
		}
		else if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			PyValue = BoolProp->GetPropertyValue(BPPtr) ? TEXT("True") : TEXT("False");
		}
		else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			PyValue = FString::SanitizeFloat(FloatProp->GetPropertyValue(BPPtr));
		}
		else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			PyValue = FString::SanitizeFloat(DoubleProp->GetPropertyValue(BPPtr));
		}
		else if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			PyValue = FString::FromInt(IntProp->GetPropertyValue(BPPtr));
		}
		else if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			FName Val = NameProp->GetPropertyValue(BPPtr);
			if (Val.IsNone()) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(Val.ToString());
		}
		else if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			FString Val = StrProp->GetPropertyValue(BPPtr);
			if (Val.IsEmpty()) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(Val);
		}
		else
		{
			FString ExportedValue;
			Prop->ExportTextItem_Direct(ExportedValue, BPPtr, ParentPtr, BPCDO, PPF_None);
			if (ExportedValue.IsEmpty()) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(ExportedValue);
		}

		Out += FString::Printf(TEXT("bp.default(%s, %s)\n"),
			*MakePythonStringLiteral_ExportBpy(Prop->GetName()),
			*PyValue);
		bHadAny = true;
	}

	if (const UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP))
	{
		if (AnimBP->TargetSkeleton)
		{
			Out += FString::Printf(
				TEXT("bp.default(%s, %s)\n"),
				*MakePythonStringLiteral_ExportBpy(TEXT("TargetSkeleton")),
				*MakePythonStringLiteral_ExportBpy(AnimBP->TargetSkeleton->GetPathName()));
			bHadAny = true;
		}

		USkeletalMesh* PreviewMesh = nullptr;
		if (UAnimBlueprint* MutableAnimBP = const_cast<UAnimBlueprint*>(AnimBP))
		{
			PreviewMesh = MutableAnimBP->GetPreviewMesh(false);
		}
		if (!PreviewMesh && AnimBP->TargetSkeleton)
		{
			PreviewMesh = AnimBP->TargetSkeleton->GetPreviewMesh(true);
		}

		if (PreviewMesh)
		{
			Out += FString::Printf(
				TEXT("bp.default(%s, %s)\n"),
				*MakePythonStringLiteral_ExportBpy(TEXT("PreviewSkeletalMesh")),
				*MakePythonStringLiteral_ExportBpy(PreviewMesh->GetPathName()));
			bHadAny = true;
		}
	}

	Out += TEXT("\n");
	return Out;
}

// ─── GenerateInheritedComponentDefaultsSection ───────────────────────────────

FString UBPDirectExporter::GenerateInheritedComponentDefaultsSection(UBlueprint* BP)
{
	FString Out;
	Out += TEXT("# ── Inherited Component Defaults ────────────────────────\n");

	if (!BP->GeneratedClass)
	{
		Out += TEXT("\n");
		return Out;
	}

	UObject* BPCDO    = BP->GeneratedClass->GetDefaultObject(false);
	UClass*  SuperClass = BP->GeneratedClass->GetSuperClass();
	UObject* ParentCDO  = SuperClass ? SuperClass->GetDefaultObject(false) : nullptr;

	AActor* BPCDOActor     = Cast<AActor>(BPCDO);
	AActor* ParentCDOActor = Cast<AActor>(ParentCDO);

	if (!BPCDOActor || !ParentCDOActor)
	{
		Out += TEXT("\n");
		return Out;
	}

	// Build set of SCS component names — already handled by GenerateComponentsSection
	TSet<FName> SCSNames;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node)
				SCSNames.Add(Node->GetVariableName());
		}
	}

	// Get all components on the BP CDO
	TArray<UActorComponent*> BPComponents;
	BPCDOActor->GetComponents(BPComponents);

	// Get all components on the parent CDO for diffing
	TArray<UActorComponent*> ParentComponents;
	ParentCDOActor->GetComponents(ParentComponents);

	for (UActorComponent* BPComp : BPComponents)
	{
		if (!BPComp) continue;

		FName CompName = BPComp->GetFName();

		// Skip SCS components — GenerateComponentsSection covers them
		if (SCSNames.Contains(CompName)) continue;

		// Find matching component on parent CDO by name + class
		UActorComponent* ParentComp = nullptr;
		for (UActorComponent* PC : ParentComponents)
		{
			if (PC && PC->GetFName() == CompName && PC->IsA(BPComp->GetClass()))
			{
				ParentComp = PC;
				break;
			}
		}
		if (!ParentComp) continue;

		// Diff properties against parent component
		TArray<FString> Entries;

		for (TFieldIterator<FProperty> It(BPComp->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Prop = *It;
			if (!Prop) continue;
			if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly)) continue;
			const void* ValuePtr   = Prop->ContainerPtrToValuePtr<void>(BPComp);

			// Only diff against parent if the parent component's class actually owns this property
			const UClass* PropOwnerClass = CastField<FProperty>(Prop) ? Prop->GetOwnerClass() : nullptr;
			const void* DefaultPtr = (PropOwnerClass && ParentComp->GetClass()->IsChildOf(PropOwnerClass))
				? Prop->ContainerPtrToValuePtr<void>(ParentComp) : nullptr;

			if (!ValuePtr || !DefaultPtr) continue;
			if (Prop->Identical(ValuePtr, DefaultPtr)) continue;

			FString PyValue;

			if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				UObject* ObjVal = ObjProp->GetObjectPropertyValue(ValuePtr);
				if (!ObjVal) continue;
				// Skip internal CDO subobject references (e.g. AttachParent pointing to Default__ paths)
				if (ObjVal->IsDefaultSubobject()) continue;
				if (ObjVal->GetPathName().Contains(TEXT("Default__"))) continue;
				PyValue = MakePythonStringLiteral_ExportBpy(ObjVal->GetPathName());
			}
			else if (const FClassProperty* ClsProp = CastField<FClassProperty>(Prop))
			{
				UClass* ClsVal = Cast<UClass>(ClsProp->GetObjectPropertyValue(ValuePtr));
				if (!ClsVal) continue;
				PyValue = MakePythonStringLiteral_ExportBpy(ClsVal->GetPathName());
			}
			else if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				PyValue = BoolProp->GetPropertyValue(ValuePtr) ? TEXT("True") : TEXT("False");
			}
			else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				PyValue = FString::SanitizeFloat(FloatProp->GetPropertyValue(ValuePtr));
			}
			else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
			{
				PyValue = FString::SanitizeFloat(DoubleProp->GetPropertyValue(ValuePtr));
			}
			else if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
			{
				PyValue = FString::FromInt(IntProp->GetPropertyValue(ValuePtr));
			}
			else if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
			{
				FName Val = NameProp->GetPropertyValue(ValuePtr);
				if (Val.IsNone()) continue;
				PyValue = MakePythonStringLiteral_ExportBpy(Val.ToString());
			}
			else if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				FString Val = StrProp->GetPropertyValue(ValuePtr);
				if (Val.IsEmpty()) continue;
				PyValue = MakePythonStringLiteral_ExportBpy(Val);
			}
			else
			{
				FString ExportedValue;
				Prop->ExportTextItem_Direct(ExportedValue, ValuePtr, DefaultPtr, BPComp, PPF_None);
				if (ExportedValue.IsEmpty()) continue;
				PyValue = MakePythonStringLiteral_ExportBpy(ExportedValue);
			}

			Entries.Add(FString::Printf(TEXT("%s: %s"),
				*MakePythonStringLiteral_ExportBpy(Prop->GetName()),
				*PyValue));
		}

		if (Entries.IsEmpty()) continue;

		FString Dict = TEXT("{");
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			Dict += Entries[i];
			if (i < Entries.Num() - 1) Dict += TEXT(", ");
		}
		Dict += TEXT("}");

		Out += FString::Printf(TEXT("bp.inherited_component(%s, properties=%s)\n"),
			*MakePythonStringLiteral_ExportBpy(CompName.ToString()),
			*Dict);
	}

	Out += TEXT("\n");
	return Out;
}

// ─── GenerateComponentsSection ────────────────────────────────────────────────

// Collect non-default property overrides from a ComponentTemplate into a Python dict literal.
// Handles object references (SkeletalMesh, AnimBlueprint, etc.) and primitive types.
static FString BuildComponentPropertiesPyDict_ExportBpy(UActorComponent* Template)
{
	if (!Template)
	{
		return TEXT("{}");
	}

	const UObject* CDO = Template->GetClass()->GetDefaultObject(false);

	// Collect entries: "PropertyName": "value"
	TArray<FString> Entries;

	for (TFieldIterator<FProperty> It(Template->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Prop = *It;
		if (!Prop) continue;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly)) continue;
		const FString PropName = Prop->GetName();
		if (PropName.Equals(TEXT("AttachParent"), ESearchCase::IgnoreCase) ||
			PropName.Equals(TEXT("AttachSocketName"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Template);
		const void* DefaultPtr = CDO ? Prop->ContainerPtrToValuePtr<void>(CDO) : nullptr;

		// Skip if same as CDO default
		if (DefaultPtr && Prop->Identical(ValuePtr, DefaultPtr))
		{
			continue;
		}

		FString PyValue;

		if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* ObjVal = ObjProp->GetObjectPropertyValue(ValuePtr);
			if (!ObjVal) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(ObjVal->GetPathName());
		}
		else if (const FClassProperty* ClsProp = CastField<FClassProperty>(Prop))
		{
			UClass* ClsVal = Cast<UClass>(ClsProp->GetObjectPropertyValue(ValuePtr));
			if (!ClsVal) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(ClsVal->GetPathName());
		}
		else if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			PyValue = BoolProp->GetPropertyValue(ValuePtr) ? TEXT("True") : TEXT("False");
		}
		else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			PyValue = FString::SanitizeFloat(FloatProp->GetPropertyValue(ValuePtr));
		}
		else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			PyValue = FString::SanitizeFloat(DoubleProp->GetPropertyValue(ValuePtr));
		}
		else if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			PyValue = FString::FromInt(IntProp->GetPropertyValue(ValuePtr));
		}
		else if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			FName Val = NameProp->GetPropertyValue(ValuePtr);
			if (Val.IsNone()) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(Val.ToString());
		}
		else if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			FString Val = StrProp->GetPropertyValue(ValuePtr);
			if (Val.IsEmpty()) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(Val);
		}
		else
		{
			// Generic fallback via ExportText
			FString ExportedValue;
			Prop->ExportTextItem_Direct(ExportedValue, ValuePtr, DefaultPtr, Template, PPF_None);
			if (ExportedValue.IsEmpty()) continue;
			PyValue = MakePythonStringLiteral_ExportBpy(ExportedValue);
		}

		Entries.Add(FString::Printf(TEXT("%s: %s"),
			*MakePythonStringLiteral_ExportBpy(Prop->GetName()),
			*PyValue));
	}

	if (Entries.IsEmpty())
	{
		return TEXT("{}");
	}

	FString Dict = TEXT("{");
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		Dict += Entries[i];
		if (i < Entries.Num() - 1) Dict += TEXT(", ");
	}
	Dict += TEXT("}");
	return Dict;
}

FString UBPDirectExporter::GenerateComponentsSection(UBlueprint* BP)
{
	FString Out;
	Out += TEXT("# ── Components ──────────────────────────────────────────\n");

	if (BP->SimpleConstructionScript)
	{
		const TArray<USCS_Node*> OrderedNodes = GetSCSNodesParentFirst_ExportBpy(BP);
		for (USCS_Node* SCSNode : OrderedNodes)
		{
			if (!SCSNode) continue;
			UClass* CompClass = SCSNode->ComponentClass;
			FString ClassName = TEXT("Unknown");
			if (CompClass)
			{
				const FString ComponentClassPath = CompClass->GetPathName();
				ClassName = ComponentClassPath.StartsWith(TEXT("/Script/"))
					? CompClass->GetName()
					: ComponentClassPath;
			}
			FString CompName = SCSNode->GetVariableName().ToString();
			const FString ParentName = ResolveComponentParentName_ExportBpy(BP, SCSNode);
			const FString AttachToName = ResolveComponentAttachToName_ExportBpy(BP, SCSNode);

			// Collect component template properties (SkeletalMesh, AnimClass, camera settings, etc.)
			FString PropertiesDict = TEXT("{}");
			if (SCSNode->ComponentTemplate)
			{
				PropertiesDict = BuildComponentPropertiesPyDict_ExportBpy(SCSNode->ComponentTemplate);
			}

			TArray<FString> ComponentArgs;
			ComponentArgs.Add(MakePythonStringLiteral_ExportBpy(CompName));
			ComponentArgs.Add(FString::Printf(TEXT("class_name=%s"), *MakePythonStringLiteral_ExportBpy(ClassName)));
			ComponentArgs.Add(FString::Printf(TEXT("parent=%s"), *MakePythonStringLiteral_ExportBpy(ParentName)));
			if (!AttachToName.IsEmpty())
			{
				ComponentArgs.Add(FString::Printf(TEXT("attach_to_name=%s"), *MakePythonStringLiteral_ExportBpy(AttachToName)));
			}
			if (PropertiesDict != TEXT("{}"))
			{
				ComponentArgs.Add(FString::Printf(TEXT("properties=%s"), *PropertiesDict));
			}

			Out += FString::Printf(TEXT("bp.component(%s)\n"), *FString::Join(ComponentArgs, TEXT(", ")));
		}
	}
	Out += TEXT("\n");
	return Out;
}

// ─── GenerateInterfacesSection ────────────────────────────────────────────────

FString UBPDirectExporter::GenerateInterfacesSection(UBlueprint* BP)
{
	FString Out;
	Out += TEXT("# ── Interfaces ──────────────────────────────────────────\n");

	for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
	{
		if (Iface.Interface)
		{
			Out += FString::Printf(
				TEXT("bp.interface(%s)\n"),
				*MakePythonStringLiteral_ExportBpy(Iface.Interface->GetPathName()));
		}
	}
	Out += TEXT("\n");
	return Out;
}

// ─── GenerateDispatchersSection ───────────────────────────────────────────────

FString UBPDirectExporter::GenerateDispatchersSection(UBlueprint* BP)
{
	FString Out;
	Out += TEXT("# ── Event Dispatchers ───────────────────────────────────\n");

	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate ||
			Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
		{
			Out += FString::Printf(TEXT("bp.dispatcher(%s)\n"), *MakePythonStringLiteral_ExportBpy(Var.VarName.ToString()));
		}
	}
	Out += TEXT("\n");
	return Out;
}

// ─── GenerateGraphFile ────────────────────────────────────────────────────────

bool UBPDirectExporter::GenerateGraphFile(
	UBlueprint* BP,
	UEdGraph* Graph,
	const FString& Prefix,
	const FString& OutDir,
	FString& OutModuleName,
	FString& OutError)
{
	TArray<FNodeInfo> NodeInfos;
	TArray<UK2Node*> K2Nodes;
	TArray<UEdGraphNode*> GenericNodes;
	TArray<UEdGraphNode*> OmittedNonK2Nodes;
	const int32 SourceNodeTotal = Graph ? Graph->Nodes.Num() : 0;

	for (UEdGraphNode* N : Graph->Nodes)
	{
		UK2Node* K2 = Cast<UK2Node>(N);
		if (K2)
		{
			K2Nodes.Add(K2);
			continue;
		}

		if (N && N->IsA<UEdGraphNode_Comment>())
		{
			GenericNodes.Add(N);
			continue;
		}

		OmittedNonK2Nodes.Add(N);
	}

	const int32 ExportableNodeTotal = K2Nodes.Num() + GenericNodes.Num();
	if (ExportableNodeTotal != SourceNodeTotal)
	{
		OutError = FString::Printf(
			TEXT("Export node count mismatch in graph '%s': source_nodes=%d exported_nodes=%d omitted_nodes=%d omitted_classes=[%s]"),
			Graph ? *Graph->GetName() : TEXT("<null>"),
			SourceNodeTotal,
			ExportableNodeTotal,
			OmittedNonK2Nodes.Num(),
			*SummarizeNodeClasses_ExportBpy(OmittedNonK2Nodes));
		return false;
	}

	K2Nodes = TopologicalSort(K2Nodes);
	for (UK2Node* Node : K2Nodes)
	{
		NodeInfos.Add(BuildNodeInfo_ExportBpy(Node));
	}
	for (UEdGraphNode* Node : GenericNodes)
	{
		NodeInfos.Add(BuildNodeInfoForGenericGraphNode_ExportBpy(Node));
	}

	AssignReadableNames(NodeInfos);
	for (int32 Index = 0; Index < K2Nodes.Num() && Index < NodeInfos.Num(); ++Index)
	{
		if (const UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(K2Nodes[Index]))
		{
			if (CompositeNode->BoundGraph)
			{
				NodeInfos[Index].NodeProps.Add(
					TEXT("BoundGraphJson"),
					SerializeJsonCompact_ExportBpy(SerializeGraph(CompositeNode->BoundGraph)));
			}
		}
	}

	// ── Determine graph context header ─────────────────────────────────────────
	UBlueprint* OwnerBP = Cast<UBlueprint>(Graph->GetOuter());
	const FString RootGraphType = GetRootGraphType_ExportBpy(OwnerBP, Graph);
	const bool bIsFunction = RootGraphType == TEXT("function");
	const bool bIsMacro = RootGraphType == TEXT("macro");

	FString GraphName = Graph->GetName();
	OutModuleName = BuildModuleName_ExportBpy(Prefix, GraphName);
	const FString MetaModuleName = OutModuleName + TEXT("_meta");
	TMap<FString, TMap<FString, FString>> ExternalizedNodePropModules;
	TMap<FString, int32> NestedModuleStemCounts;
	for (FNodeInfo& Info : NodeInfos)
	{
		TArray<FString> PropKeys;
		Info.NodeProps.GetKeys(PropKeys);
		PropKeys.Sort();
		for (const FString& PropKey : PropKeys)
		{
			if (!ShouldExternalizeNodeProp_ExportBpy(PropKey))
			{
				continue;
			}

			const FString* JsonText = Info.NodeProps.Find(PropKey);
			if (!JsonText || JsonText->IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> NestedGraphJson;
			if (!ParseJsonObject_ExportBpy(*JsonText, NestedGraphJson) || !NestedGraphJson.IsValid())
			{
				continue;
			}

			FString NestedGraphName;
			NestedGraphJson->TryGetStringField(TEXT("name"), NestedGraphName);
			if (NestedGraphName.IsEmpty())
			{
				NestedGraphName = PropKey;
			}

			const FString DesiredModuleStem = BuildNestedGraphModuleStem_ExportBpy(
				OutModuleName,
				Info.NodeType,
				NestedGraphName);
			const FString ModuleStem = MakeUniqueNestedGraphModuleStem_ExportBpy(
				DesiredModuleStem,
				NestedModuleStemCounts);
			if (!WriteNestedGraphModule_ExportBpy(
				OutDir,
				ModuleStem,
				NestedModuleStemCounts,
				NestedGraphJson,
				OutError))
			{
				return false;
			}

			ExternalizedNodePropModules.FindOrAdd(Info.VarName).Add(PropKey, ModuleStem);
		}
	}

	TMap<UK2Node*, FString> NodeVarMap;
	TMap<FString, FString> NodeGuidMap;
	TMap<FString, FVector2D> NodePosMap;
	TMap<FString, FString> PinAliasMap;
	TMap<FString, FString> PinIdMap;
	TMap<FString, FString> InputPinTypeMap;
	TMap<FString, FString> OutputPinTypeMap;
	TMap<FString, TMap<FString, FString>> NodePropsMap;
	for (int32 i = 0; i < K2Nodes.Num() && i < NodeInfos.Num(); i++)
	{
		NodeVarMap.Add(K2Nodes[i], NodeInfos[i].VarName);
		NodeGuidMap.Add(NodeInfos[i].VarName, NodeInfos[i].NodeGuid);
		NodePosMap.Add(NodeInfos[i].VarName, NodeInfos[i].Position);

		for (const TPair<FString, FString>& AliasEntry : NodeInfos[i].PinAliases)
		{
			PinAliasMap.Add(NodeInfos[i].VarName + TEXT(".") + AliasEntry.Key, AliasEntry.Value);
		}
		for (const TPair<FString, FString>& PinIdEntry : NodeInfos[i].PinIds)
		{
			PinIdMap.Add(NodeInfos[i].VarName + TEXT(".") + PinIdEntry.Key, PinIdEntry.Value);
		}
		for (const TPair<FString, FString>& PinTypeEntry : NodeInfos[i].InputPinTypes)
		{
			InputPinTypeMap.Add(NodeInfos[i].VarName + TEXT(".") + PinTypeEntry.Key, PinTypeEntry.Value);
		}
		for (const TPair<FString, FString>& PinTypeEntry : NodeInfos[i].OutputPinTypes)
		{
			OutputPinTypeMap.Add(NodeInfos[i].VarName + TEXT(".") + PinTypeEntry.Key, PinTypeEntry.Value);
		}
		if (NodeInfos[i].NodeProps.Num() > 0)
		{
			TMap<FString, FString> FilteredNodeProps = NodeInfos[i].NodeProps;
			if (const TMap<FString, FString>* ExternalizedProps = ExternalizedNodePropModules.Find(NodeInfos[i].VarName))
			{
				for (const TPair<FString, FString>& ExternalizedEntry : *ExternalizedProps)
				{
					FilteredNodeProps.Remove(ExternalizedEntry.Key);
				}
			}
			if (FilteredNodeProps.Num() > 0)
			{
				NodePropsMap.Add(NodeInfos[i].VarName, FilteredNodeProps);
			}
		}
	}

	// Generic graph nodes (for example EdGraphNode_Comment) are appended after
	// K2 nodes in NodeInfos. Persist their guid/position/node_props into META too.
	for (int32 i = K2Nodes.Num(); i < NodeInfos.Num(); ++i)
	{
		NodeGuidMap.Add(NodeInfos[i].VarName, NodeInfos[i].NodeGuid);
		NodePosMap.Add(NodeInfos[i].VarName, NodeInfos[i].Position);
		if (NodeInfos[i].NodeProps.Num() > 0)
		{
			TMap<FString, FString> FilteredNodeProps = NodeInfos[i].NodeProps;
			if (const TMap<FString, FString>* ExternalizedProps = ExternalizedNodePropModules.Find(NodeInfos[i].VarName))
			{
				for (const TPair<FString, FString>& ExternalizedEntry : *ExternalizedProps)
				{
					FilteredNodeProps.Remove(ExternalizedEntry.Key);
				}
			}
			if (FilteredNodeProps.Num() > 0)
			{
				NodePropsMap.Add(NodeInfos[i].VarName, FilteredNodeProps);
			}
		}
	}

	FString Lines;
	Lines += TEXT("# Auto-generated by ExportBpy\n");
	Lines += TEXT("import importlib.util\n");
	Lines += TEXT("import os\n");
	Lines += TEXT("from ue_bp_dsl import *\n");
	Lines += TEXT("\n");
	Lines += TEXT("def _load_sidecar_graph(stem):\n");
	Lines += TEXT("    path = os.path.join(os.path.dirname(__file__), f\"{stem}.bp.py\")\n");
	Lines += TEXT("    spec = importlib.util.spec_from_file_location(f\"_exportbpy_sidecar_{stem}\", path)\n");
	Lines += TEXT("    if spec is None or spec.loader is None:\n");
	Lines += TEXT("        raise ImportError(f\"Cannot load sidecar graph module: {path}\")\n");
	Lines += TEXT("    module = importlib.util.module_from_spec(spec)\n");
	Lines += TEXT("    spec.loader.exec_module(module)\n");
	Lines += TEXT("    return getattr(module, \"GRAPH\")\n\n");
	Lines += TEXT("def _load_meta():\n");
	Lines += FString::Printf(TEXT("    meta_path = os.path.join(os.path.dirname(__file__), %s)\n"), *MakePythonStringLiteral_ExportBpy(MetaModuleName + TEXT(".py")));
	Lines += FString::Printf(TEXT("    spec = importlib.util.spec_from_file_location(%s, meta_path)\n"), *MakePythonStringLiteral_ExportBpy(TEXT("_exportbpy_meta_") + MetaModuleName));
	Lines += TEXT("    if spec is None or spec.loader is None:\n");
	Lines += TEXT("        raise ImportError(f\"Cannot load meta module: {meta_path}\")\n");
	Lines += TEXT("    module = importlib.util.module_from_spec(spec)\n");
	Lines += TEXT("    spec.loader.exec_module(module)\n");
	Lines += TEXT("    return getattr(module, \"META\", {})\n\n");
	Lines += TEXT("META = _load_meta()\n\n");
	Lines += TEXT("def register(bp):\n");

	FString CtxHeader;
	if (bIsFunction)
	{
		FString InputsStr, OutputsStr;
		bool bIsPure = false;
		bool bThreadSafe = false;
		FString FunctionCategory;
		for (UK2Node* K2 : K2Nodes)
		{
			if (auto* FE = Cast<UK2Node_FunctionEntry>(K2))
			{
				bIsPure |= (FE->GetFunctionFlags() & FUNC_BlueprintPure) != 0;
				bThreadSafe |= FE->MetaData.bThreadSafe;
				if (FunctionCategory.IsEmpty())
				{
					FunctionCategory = ResolveFunctionCategory_ExportBpy(Graph, FE);
				}
				bool bFirst = true;
				for (UEdGraphPin* Pin : FE->Pins)
				{
					if (Pin->Direction == EGPD_Output &&
						Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
						!Pin->PinName.IsNone())
					{
						if (!bFirst) InputsStr += TEXT(", ");
						InputsStr += FString::Printf(TEXT("(%s, %s)"),
							*MakePythonStringLiteral_ExportBpy(Pin->PinName.ToString()),
							*MakePythonStringLiteral_ExportBpy(NormalizeTypeString_ExportBpy(Pin->PinType)));
						bFirst = false;
					}
				}
			}
			else if (auto* FR = Cast<UK2Node_FunctionResult>(K2))
			{
				// Only use the first FunctionResult node for the signature
				if (OutputsStr.IsEmpty())
				{
					bool bFirst = true;
					for (UEdGraphPin* Pin : FR->Pins)
					{
						if (Pin->Direction == EGPD_Input &&
							Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
							!Pin->PinName.IsNone())
						{
							if (!bFirst) OutputsStr += TEXT(", ");
							OutputsStr += FString::Printf(TEXT("(%s, %s)"),
								*MakePythonStringLiteral_ExportBpy(Pin->PinName.ToString()),
								*MakePythonStringLiteral_ExportBpy(NormalizeTypeString_ExportBpy(Pin->PinType)));
							bFirst = false;
						}
					}
				}
			}
		}
		FString Args = MakePythonStringLiteral_ExportBpy(GraphName);
		if (Graph->GraphGuid.IsValid())
		{
			Args += FString::Printf(
				TEXT(", graph_guid=%s"),
				*MakePythonStringLiteral_ExportBpy(Graph->GraphGuid.ToString(EGuidFormats::Digits)));
		}
		if (!InputsStr.IsEmpty())
			Args += FString::Printf(TEXT(", inputs=[%s]"), *InputsStr);
		if (!OutputsStr.IsEmpty())
			Args += FString::Printf(TEXT(", outputs=[%s]"), *OutputsStr);
		if (bIsPure)
			Args += TEXT(", pure=True");
		if (bThreadSafe)
			Args += TEXT(", thread_safe=True");
		if (!FunctionCategory.IsEmpty())
			Args += FString::Printf(TEXT(", category=%s"), *MakePythonStringLiteral_ExportBpy(FunctionCategory));
		CtxHeader = FString::Printf(TEXT("with bp.function(%s) as g:"), *Args);
	}
	else if (bIsMacro)
	{
		FString InputsStr;
		FString OutputsStr;
		TArray<UK2Node_Tunnel*> TunnelNodes;
		Graph->GetNodesOfClass(TunnelNodes);
		for (const UK2Node_Tunnel* TunnelNode : TunnelNodes)
		{
			if (!TunnelNode)
			{
				continue;
			}

			const bool bIsEntryTunnel = TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs;
			const bool bIsExitTunnel = TunnelNode->bCanHaveInputs && !TunnelNode->bCanHaveOutputs;
			if (!bIsEntryTunnel && !bIsExitTunnel)
			{
				continue;
			}

			FString& TargetPins = bIsEntryTunnel ? InputsStr : OutputsStr;
			bool bFirst = TargetPins.IsEmpty();
			for (UEdGraphPin* Pin : TunnelNode->Pins)
			{
				const bool bMatchesDirection =
					(bIsEntryTunnel && Pin->Direction == EGPD_Output) ||
					(bIsExitTunnel && Pin->Direction == EGPD_Input);
				if (!bMatchesDirection || Pin->PinName.IsNone())
				{
					continue;
				}

				if (!bFirst)
				{
					TargetPins += TEXT(", ");
				}

				TargetPins += FString::Printf(
					TEXT("(%s, %s)"),
					*MakePythonStringLiteral_ExportBpy(Pin->PinName.ToString()),
					*MakePythonStringLiteral_ExportBpy(NormalizeTypeString_ExportBpy(Pin->PinType)));
				bFirst = false;
			}
		}

		FString Args = MakePythonStringLiteral_ExportBpy(GraphName);
		if (Graph->GraphGuid.IsValid())
		{
			Args += FString::Printf(
				TEXT(", graph_guid=%s"),
				*MakePythonStringLiteral_ExportBpy(Graph->GraphGuid.ToString(EGuidFormats::Digits)));
		}
		if (!InputsStr.IsEmpty())
		{
			Args += FString::Printf(TEXT(", inputs=[%s]"), *InputsStr);
		}
		if (!OutputsStr.IsEmpty())
		{
			Args += FString::Printf(TEXT(", outputs=[%s]"), *OutputsStr);
		}
		CtxHeader = FString::Printf(TEXT("with bp.macro(%s) as g:"), *Args);
	}
	else
	{
		FString Args = MakePythonStringLiteral_ExportBpy(GraphName);
		if (Graph->GraphGuid.IsValid())
		{
			Args += FString::Printf(
				TEXT(", graph_guid=%s"),
				*MakePythonStringLiteral_ExportBpy(Graph->GraphGuid.ToString(EGuidFormats::Digits)));
		}
		CtxHeader = FString::Printf(TEXT("with bp.event_graph(%s) as g:"), *Args);
	}

	Lines += TEXT("    ") + CtxHeader + TEXT("\n\n");

	auto AppendConnectionSections = [&](FString& InOutLines)
	{
		TArray<FString> DataLines;
		TArray<FString> ExecLines;
		TSet<FString> SeenConnections;

		auto TryAppendConnection = [&](UK2Node* SrcNode, const FString& SrcVar, UEdGraphPin* SrcPin, UEdGraphPin* RawDst)
		{
			TArray<UEdGraphPin*> DestinationPins;
			const bool bSrcIsKnot = SrcNode && SrcNode->IsA<UK2Node_Knot>();
			const bool bDstIsKnot = RawDst && RawDst->GetOwningNode() && RawDst->GetOwningNode()->IsA<UK2Node_Knot>();
			if (bSrcIsKnot || bDstIsKnot)
			{
				if (RawDst)
				{
					DestinationPins.Add(RawDst);
				}
			}
			else
			{
				ResolveRerouteChainAll(RawDst, DestinationPins);
			}

			for (UEdGraphPin* DstPin : DestinationPins)
			{
				UK2Node* DstNode = Cast<UK2Node>(DstPin->GetOwningNode());
				if (!DstNode)
				{
					continue;
				}

				const FString* DstVar = NodeVarMap.Find(DstNode);
				if (!DstVar)
				{
					continue;
				}

				const FString SrcRef = TranslateOutputPinRef_ExportBpy(SrcNode, SrcVar, SrcPin);
				const FString DstRef = TranslateInputPinRef_ExportBpy(DstNode, *DstVar, DstPin);
				const FString ConnLine = SrcRef + TEXT(" >> ") + DstRef;
				if (SeenConnections.Contains(ConnLine))
				{
					continue;
				}

				SeenConnections.Add(ConnLine);
				if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					ExecLines.Add(ConnLine);
				}
				else
				{
					DataLines.Add(ConnLine);
				}
			}
		};

		for (int32 Index = 0; Index < K2Nodes.Num() && Index < NodeInfos.Num(); ++Index)
		{
			UK2Node* SrcNode = K2Nodes[Index];
			const FString& SrcVar = NodeInfos[Index].VarName;
			for (UEdGraphPin* SrcPin : SrcNode->Pins)
			{
				if (SrcPin->Direction != EGPD_Output)
				{
					continue;
				}

				for (UEdGraphPin* RawDst : SrcPin->LinkedTo)
				{
					TryAppendConnection(SrcNode, SrcVar, SrcPin, RawDst);
				}

				if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					for (UEdGraphPin* SyntheticDst : GetSyntheticExecPassThroughTargets_ExportBpy(SrcNode, SrcPin))
					{
						TryAppendConnection(SrcNode, SrcVar, SrcPin, SyntheticDst);
					}
				}
			}
		}

		if (!DataLines.IsEmpty())
		{
			InOutLines += TEXT("        # Data flow\n");
			for (const FString& Line : DataLines)
			{
				InOutLines += TEXT("        ") + Line + TEXT("\n");
			}
			InOutLines += TEXT("\n");
		}

		if (!ExecLines.IsEmpty())
		{
			InOutLines += TEXT("        # Exec flow\n");
			for (const FString& Line : ExecLines)
			{
				InOutLines += TEXT("        ") + Line + TEXT("\n");
			}
			InOutLines += TEXT("\n");
		}
	};

	Lines += TEXT("        # Nodes\n");
	if (NodeInfos.IsEmpty())
	{
		Lines += TEXT("        pass\n\n");
	}
	else
	{
		for (const FNodeInfo& Info : NodeInfos)
		{
			Lines += TEXT("        ") + NodeToCtorLine(Info) + TEXT("\n");
			for (const FString& DefaultLine : NodeToDefaultValueLines(Info))
			{
				Lines += TEXT("        ") + DefaultLine + TEXT("\n");
			}
			for (const FString& ExtraPropLine : InlineExtraPropLines_ExportBpy(Info))
			{
				Lines += TEXT("        ") + ExtraPropLine + TEXT("\n");
			}
			if (const TMap<FString, FString>* ExternalizedProps = ExternalizedNodePropModules.Find(Info.VarName))
			{
				TArray<FString> ExternalizedKeys;
				ExternalizedProps->GetKeys(ExternalizedKeys);
				ExternalizedKeys.Sort();
				for (const FString& PropKey : ExternalizedKeys)
				{
					const FString* ModuleStem = ExternalizedProps->Find(PropKey);
					if (!ModuleStem || ModuleStem->IsEmpty())
					{
						continue;
					}

					Lines += FString::Printf(
						TEXT("        %s.set_extra_prop(%s, nested_graph_prop(_load_sidecar_graph(%s)))\n"),
						*Info.VarName,
						*MakePythonStringLiteral_ExportBpy(PropKey),
						*MakePythonStringLiteral_ExportBpy(*ModuleStem));
				}
			}
		}
		Lines += TEXT("\n");
	}

	AppendConnectionSections(Lines);

	FString MetaLines;
	MetaLines += TEXT("# Auto-generated by ExportBpy\n\n");
	MetaLines += TEXT("# pin_alias: maps DSL_clean_name -> UE_actual_pin_name\n");
	MetaLines += TEXT("META = {\n");
	AppendStringMapSection_ExportBpy(MetaLines, TEXT("node_guid"), NodeGuidMap);
	AppendVectorMapSection_ExportBpy(MetaLines, TEXT("node_pos"), NodePosMap);
	AppendStringMapSection_ExportBpy(MetaLines, TEXT("pin_alias"), PinAliasMap);
	AppendStringMapSection_ExportBpy(MetaLines, TEXT("pin_id"), PinIdMap);
	AppendStringMapSection_ExportBpy(MetaLines, TEXT("input_pin_types"), InputPinTypeMap);
	AppendStringMapSection_ExportBpy(MetaLines, TEXT("output_pin_types"), OutputPinTypeMap);
	AppendNestedMapSection_ExportBpy(MetaLines, TEXT("node_props"), NodePropsMap, false);
	MetaLines += TEXT("}\n");

	const FString MetaPath = FPaths::Combine(OutDir, MetaModuleName + TEXT(".py"));
	if (!FFileHelper::SaveStringToFile(MetaLines, *MetaPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Cannot write %s"), *MetaPath);
		return false;
	}

	const FString OutPath = FPaths::Combine(OutDir, OutModuleName + TEXT(".bp.py"));
	if (!FFileHelper::SaveStringToFile(Lines, *OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Cannot write %s"), *OutPath);
		return false;
	}
	return true;
}

// ─── SplitEventGraphByEntryPoints ─────────────────────────────────────────────

TArray<TArray<UK2Node*>> UBPDirectExporter::SplitEventGraphByEntryPoints(UEdGraph* Graph)
{
	TArray<TArray<UK2Node*>> Result;

	// Collect all entry nodes (Event / CustomEvent)
	TArray<UK2Node*> EntryNodes;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N->IsA<UK2Node_Event>() || N->IsA<UK2Node_CustomEvent>())
		{
			if (UK2Node* K2 = Cast<UK2Node>(N))
				EntryNodes.Add(K2);
		}
	}

	// BFS from each entry node following exec pins
	for (UK2Node* Entry : EntryNodes)
	{
		TArray<UK2Node*> Cluster;
		TSet<UK2Node*> Visited;
		TQueue<UK2Node*> Queue;
		Queue.Enqueue(Entry);
		Visited.Add(Entry);

		while (!Queue.IsEmpty())
		{
			UK2Node* Cur;
			Queue.Dequeue(Cur);
			Cluster.Add(Cur);

			for (UEdGraphPin* Pin : Cur->Pins)
			{
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					UK2Node* Next = Cast<UK2Node>(Linked->GetOwningNode());
					if (Next && !Visited.Contains(Next))
					{
						Visited.Add(Next);
						Queue.Enqueue(Next);
					}
				}
			}
		}

		Result.Add(MoveTemp(Cluster));
	}

	return Result;
}

// ─── NodeToCtorLine ───────────────────────────────────────────────────────────

FString UBPDirectExporter::NodeToCtorLine(const FNodeInfo& Info)
{
	const FString& NodeType = Info.NodeType;

	if (NodeType == TEXT("K2Node_FunctionEntry"))
	{
		return FString::Printf(TEXT("%s = g.entry()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_FunctionResult"))
	{
		return FString::Printf(TEXT("%s = g.result()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_IfThenElse"))
	{
		return FString::Printf(TEXT("%s = g.branch()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_ExecutionSequence"))
	{
		return FString::Printf(TEXT("%s = g.sequence()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_VariableGet"))
	{
		return FString::Printf(TEXT("%s = g.get_var(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_VariableSet"))
	{
		return FString::Printf(TEXT("%s = g.set_var(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_Message"))
	{
		FString FunctionRef = Info.FunctionName;
		if (!Info.ClassName.IsEmpty())
		{
			FunctionRef = Info.ClassName + TEXT("::") + Info.FunctionName;
		}
		return FString::Printf(TEXT("%s = g.message(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(FunctionRef));
	}
	if (Info.bIsCallFunctionLike)
	{
		FString FunctionRef = Info.FunctionName;
		if (!Info.ClassName.IsEmpty())
		{
			FunctionRef = Info.ClassName + TEXT("::") + Info.FunctionName;
		}
		if (NodeType != TEXT("K2Node_CallFunction"))
		{
			return FString::Printf(
				TEXT("%s = g.call(%s, node_class=%s)"),
				*Info.VarName,
				*MakePythonStringLiteral_ExportBpy(FunctionRef),
				*MakePythonStringLiteral_ExportBpy(NodeType));
		}
		return FString::Printf(TEXT("%s = g.call(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(FunctionRef));
	}
	if (NodeType == TEXT("K2Node_Event"))
	{
		return FString::Printf(TEXT("%s = g.event(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_CustomEvent"))
	{
		if (Info.CustomParams.Num() > 0)
		{
			TArray<FString> ParamLiterals;
			ParamLiterals.Reserve(Info.CustomParams.Num());
			for (const TPair<FString, FString>& Param : Info.CustomParams)
			{
				ParamLiterals.Add(
					FString::Printf(
						TEXT("(%s, %s)"),
						*MakePythonStringLiteral_ExportBpy(Param.Key),
						*MakePythonStringLiteral_ExportBpy(Param.Value)));
			}

			return FString::Printf(
				TEXT("%s = g.custom_event(%s, params=[%s])"),
				*Info.VarName,
				*MakePythonStringLiteral_ExportBpy(Info.FunctionName),
				*FString::Join(ParamLiterals, TEXT(", ")));
		}

		return FString::Printf(TEXT("%s = g.custom_event(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (NodeType == TEXT("K2Node_DynamicCast"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.cast(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
	}
	if (NodeType == TEXT("K2Node_Select"))
	{
		return FString::Printf(TEXT("%s = g.select()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_SwitchEnum"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.switch_enum(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
		return FString::Printf(TEXT("%s = g.switch_enum()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_SwitchInteger"))
	{
		return FString::Printf(TEXT("%s = g.switch_int()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_BreakStruct"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.break_struct(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
		return FString::Printf(TEXT("%s = g.break_struct()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_MakeStruct"))
	{
		if (!Info.TargetType.IsEmpty())
		{
			return FString::Printf(TEXT("%s = g.make_struct(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.TargetType));
		}
		return FString::Printf(TEXT("%s = g.make_struct()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_Self"))
	{
		return FString::Printf(TEXT("%s = g.self_ref()"), *Info.VarName);
	}
	if (NodeType == TEXT("K2Node_CallDelegate"))
	{
		return FString::Printf(TEXT("%s = g.call_dispatcher(%s)"), *Info.VarName, *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}

	FString TypeStr = NodeType;
	TypeStr.RemoveFromStart(TEXT("K2Node_"));

	FString Args = FString::Printf(TEXT("type=%s"), *MakePythonStringLiteral_ExportBpy(TypeStr));
	if (!Info.FunctionName.IsEmpty())
	{
		Args += FString::Printf(TEXT(", name=%s"), *MakePythonStringLiteral_ExportBpy(Info.FunctionName));
	}
	if (!Info.ClassName.IsEmpty())
	{
		Args += FString::Printf(TEXT(", class_name=%s"), *MakePythonStringLiteral_ExportBpy(Info.ClassName));
	}
	if (!Info.TargetType.IsEmpty())
	{
		Args += FString::Printf(TEXT(", target_type=%s"), *MakePythonStringLiteral_ExportBpy(Info.TargetType));
	}

	return FString::Printf(TEXT("%s = g.node(%s)"), *Info.VarName, *Args);
}

// ─── NodeToDefaultValueLines ─────────────────────────────────────────────────

TArray<FString> UBPDirectExporter::NodeToDefaultValueLines(const FNodeInfo& Info)
{
	TArray<FString> Lines;
	for (auto& KV : Info.DefaultValues)
	{
		const FString* PinType = Info.InputPinTypes.Find(KV.Key);
		const FString FormattedLiteral = ForceRealLiteralIfNeeded_ExportBpy(
			FormatPythonValueLiteral_ExportBpy(KV.Value),
			PinType ? *PinType : FString());
		Lines.Add(FString::Printf(TEXT("%s.pin(%s, %s)"),
			*Info.VarName,
			*MakePythonStringLiteral_ExportBpy(KV.Key),
			*FormattedLiteral));
	}
	return Lines;
}

// ─── PinConnectionToLine ─────────────────────────────────────────────────────

FString UBPDirectExporter::PinConnectionToLine(
	const FString& SrcVar, const FString& SrcPin,
	const FString& DstVar, const FString& DstPin)
{
	return FString::Printf(TEXT("connect(%s, \"%s\", %s, \"%s\")"),
		*SrcVar, *SrcPin, *DstVar, *DstPin);
}

// ─── AssignReadableNames ──────────────────────────────────────────────────────

void UBPDirectExporter::AssignReadableNames(TArray<FNodeInfo>& Nodes)
{
	TMap<FString, int32> NameCount;
	for (FNodeInfo& Info : Nodes)
	{
		FString BaseName;
		bool bAlwaysNumber = false;

		if (Info.NodeType == TEXT("K2Node_FunctionEntry"))
		{
			BaseName = TEXT("Entry");
		}
		else if (Info.NodeType == TEXT("K2Node_FunctionResult"))
		{
			BaseName = TEXT("Return");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_IfThenElse"))
		{
			BaseName = TEXT("Branch");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_SwitchEnum"))
		{
			BaseName = TEXT("SwitchEnum");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_SwitchInteger"))
		{
			BaseName = TEXT("SwitchInt");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_Select"))
		{
			BaseName = TEXT("Select");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_BreakStruct"))
		{
			BaseName = TEXT("BreakStruct");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_MakeStruct"))
		{
			BaseName = TEXT("MakeStruct");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_DynamicCast"))
		{
			BaseName = TEXT("DynamicCast");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_ExecutionSequence"))
		{
			BaseName = TEXT("Sequence");
			bAlwaysNumber = true;
		}
		else if (Info.NodeType == TEXT("K2Node_VariableSet"))
		{
			BaseName = TEXT("Set_") + Info.FunctionName;
		}
		else if (!Info.FunctionName.IsEmpty())
		{
			BaseName = Info.FunctionName;
		}
		else
		{
			BaseName = Info.NodeType;
			BaseName.RemoveFromStart(TEXT("K2Node_"));
		}

		const FString SafeBase = SanitizePythonIdentifier_ExportBpy(BaseName, TEXT("Node"));
		int32& Count = NameCount.FindOrAdd(SafeBase);
		if (bAlwaysNumber)
		{
			Info.VarName = FString::Printf(TEXT("%s_%d"), *SafeBase, Count);
		}
		else if (Count == 0)
		{
			Info.VarName = SafeBase;
		}
		else
		{
			Info.VarName = FString::Printf(TEXT("%s_%d"), *SafeBase, Count);
		}
		++Count;
	}
}

// ─── TopologicalSort ─────────────────────────────────────────────────────────

TArray<UK2Node*> UBPDirectExporter::TopologicalSort(const TArray<UK2Node*>& Nodes)
{
	// Build adjacency via exec pins
	TMap<UK2Node*, TSet<UK2Node*>> Deps;
	TSet<UK2Node*> NodeSet(Nodes);

	for (UK2Node* N : Nodes)
		Deps.FindOrAdd(N);

	for (UK2Node* N : Nodes)
	{
		for (UEdGraphPin* Pin : N->Pins)
		{
			if (Pin->Direction != EGPD_Output) continue;
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				UK2Node* Dst = Cast<UK2Node>(Linked->GetOwningNode());
				if (Dst && NodeSet.Contains(Dst))
					Deps[Dst].Add(N);
			}
		}
	}

	TArray<UK2Node*> Sorted;
	TSet<UK2Node*> Visited;

	TFunction<void(UK2Node*)> Visit = [&](UK2Node* N)
	{
		if (Visited.Contains(N)) return;
		Visited.Add(N);
		for (UK2Node* Dep : Deps[N])
			Visit(Dep);
		Sorted.Add(N);
	};

	for (UK2Node* N : Nodes)
		Visit(N);

	return Sorted;
}

// ─── ResolveRerouteChain ─────────────────────────────────────────────────────

UEdGraphPin* UBPDirectExporter::ResolveRerouteChain(UEdGraphPin* Pin)
{
	TArray<UEdGraphPin*> ResolvedPins;
	ResolveRerouteChainAll(Pin, ResolvedPins);
	return ResolvedPins.IsEmpty() ? nullptr : ResolvedPins[0];
}

void UBPDirectExporter::ResolveRerouteChainAll(UEdGraphPin* Pin, TArray<UEdGraphPin*>& OutPins)
{
	OutPins.Reset();
	if (!Pin)
	{
		return;
	}

	TArray<UEdGraphPin*> Stack;
	Stack.Add(Pin);

	TSet<UEdGraphPin*> Visited;
	const int32 MaxDepth = 256;
	int32 Depth = 0;

	while (!Stack.IsEmpty() && Depth++ < MaxDepth)
	{
		UEdGraphPin* Current = Stack.Pop(false);
		if (!Current || Visited.Contains(Current))
		{
			continue;
		}

		Visited.Add(Current);
		UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Current->GetOwningNode());
		if (!Knot)
		{
			OutPins.AddUnique(Current);
			continue;
		}

		UEdGraphPin* OutPin = Knot->GetOutputPin();
		if (!OutPin || OutPin->LinkedTo.IsEmpty())
		{
			continue;
		}

		for (UEdGraphPin* LinkedPin : OutPin->LinkedTo)
		{
			if (LinkedPin && !Visited.Contains(LinkedPin))
			{
				Stack.Add(LinkedPin);
			}
		}
	}
}

// ─── ReadBlueprintToJson ──────────────────────────────────────────────────────

FString UBPDirectExporter::ReadBlueprintToJson(const FString& BlueprintPath)
{
	// Load blueprint
	UBlueprint* BP = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!BP)
		BP = Cast<UBlueprint>(
			StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!BP)
		return FString();

	TSharedPtr<FJsonObject> Root = SerializeBlueprintToJson(BP);
	if (!Root.IsValid())
		return FString();

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

// ─── ExportStandaloneAssetToPy ───────────────────────────────────────────────

bool UBPDirectExporter::ExportStandaloneAssetToPy(
	const FString& AssetPath,
	const FString& OutputDir,
	FString& OutError)
{
	const FString ObjectPath = NormalizeStandaloneAssetObjectPath_ExportBpy(AssetPath);
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	if (!Asset)
	{
		const FString PackagePath = FPackageName::ObjectPathToPackageName(ObjectPath);
		if (!PackagePath.IsEmpty())
		{
			Asset = Cast<UObject>(UEditorAssetLibrary::LoadAsset(PackagePath));
		}
	}
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Cannot load asset: %s"), *ObjectPath);
		return false;
	}

	const TSharedPtr<FJsonObject> Meta = BuildStandaloneAssetMeta_ExportBpy(Asset);
	FString Content;
	Content += TEXT("# Auto-generated by ExportBpy\n\n");
	Content += TEXT("META = ");
	Content += SerializeJsonPretty_ExportBpy(Meta);
	Content += TEXT("\n");

	// Write to OutputDir/__asset__.meta.py (one file per asset, named after the asset)
	const FString SafeName = FPaths::MakeValidFileName(Asset->GetName());
	const FString OutPath = FPaths::Combine(OutputDir, SafeName + TEXT("__asset__.meta.py"));

	IFileManager::Get().MakeDirectory(*OutputDir, true);
	if (!FFileHelper::SaveStringToFile(Content, *OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Cannot write %s"), *OutPath);
		return false;
	}

	return true;
}

// ─── SerializeBlueprintToJson ─────────────────────────────────────────────────

TSharedPtr<FJsonObject> UBPDirectExporter::SerializeBlueprintToJson(UBlueprint* BP)
{
	auto Root = MakeShared<FJsonObject>();

	// Meta
	FString BPName = FPaths::GetBaseFilename(BP->GetPathName());
	Root->SetStringField(TEXT("path"),    BP->GetPathName());
	Root->SetStringField(TEXT("parent"),  BP->ParentClass ? BP->ParentClass->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("bp_type"), BP->IsA<UAnimBlueprint>() ? TEXT("AnimBlueprint") : TEXT("Normal"));
	Root->SetStringField(TEXT("name"),    BPName);

	// Interfaces
	TArray<TSharedPtr<FJsonValue>> Interfaces;
	for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
		if (Iface.Interface)
			Interfaces.Add(MakeShared<FJsonValueString>(Iface.Interface->GetPathName()));
	Root->SetArrayField(TEXT("interfaces"), Interfaces);

	// Variables
	TArray<TSharedPtr<FJsonValue>> Vars;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		auto VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("name"),     Var.VarName.ToString());
		const FString TypeStr = NormalizeTypeString_ExportBpy(Var.VarType);
		const FString DefaultValue = GetBlueprintVariableDefaultValue_ExportBpy(BP, Var);
		VObj->SetStringField(TEXT("type"),     TypeStr);
		VObj->SetStringField(TEXT("container"),
			Var.VarType.IsArray() ? TEXT("array") :
			Var.VarType.IsSet() ? TEXT("set") :
			Var.VarType.IsMap() ? TEXT("map") : TEXT("single"));
		VObj->SetStringField(TEXT("default"),  DefaultValue);
		VObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VObj->SetBoolField(TEXT("category_explicit"), true);
		VObj->SetBoolField(TEXT("replicated"), (Var.PropertyFlags & CPF_Net) != 0);
		VObj->SetBoolField(TEXT("replicated_explicit"), true);
		VObj->SetStringField(TEXT("rep_notify"), Var.RepNotifyFunc.ToString());
		VObj->SetBoolField(TEXT("rep_notify_explicit"), true);
		VObj->SetBoolField(TEXT("instance_editable"), (Var.PropertyFlags & CPF_Edit) != 0);
		VObj->SetBoolField(TEXT("instance_editable_explicit"), true);
		VObj->SetStringField(TEXT("tooltip"),  Var.MetaDataArray.IsEmpty() ? TEXT("") :
			Var.MetaDataArray[0].DataValue);
		VObj->SetBoolField(TEXT("tooltip_explicit"), true);
		Vars.Add(MakeShared<FJsonValueObject>(VObj));
	}
	Root->SetArrayField(TEXT("variables"), Vars);

	// Class defaults (used by strict importer roundtrip hash validation).
	TArray<TSharedPtr<FJsonValue>> ClassDefaults;
	if (BP->GeneratedClass)
	{
		UObject* BPCDO = BP->GeneratedClass->GetDefaultObject(false);
		UClass* SuperClass = BP->GeneratedClass->GetSuperClass();
		UObject* ParentCDO = (SuperClass && SuperClass->GetDefaultObject(false))
			? SuperClass->GetDefaultObject(false)
			: nullptr;

		if (BPCDO && ParentCDO)
		{
			for (TFieldIterator<FProperty> It(BP->GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				const FProperty* Prop = *It;
				if (!Prop)
				{
					continue;
				}

				if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly))
				{
					continue;
				}
				if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_Deprecated))
				{
					continue;
				}
				if (CastField<FArrayProperty>(Prop) || CastField<FSetProperty>(Prop) || CastField<FMapProperty>(Prop))
				{
					continue;
				}

				const void* BPPtr = Prop->ContainerPtrToValuePtr<void>(BPCDO);
				const UClass* PropOwnerClass = Prop->GetOwnerClass();
				const void* ParentPtr = (PropOwnerClass && ParentCDO->GetClass()->IsChildOf(PropOwnerClass))
					? Prop->ContainerPtrToValuePtr<void>(ParentCDO)
					: nullptr;
				if (!BPPtr || !ParentPtr)
				{
					continue;
				}
				if (Prop->Identical(BPPtr, ParentPtr))
				{
					continue;
				}

				FString ValueText;
				Prop->ExportTextItem_Direct(ValueText, BPPtr, ParentPtr, BPCDO, PPF_None);
				if (ValueText.IsEmpty())
				{
					continue;
				}

				TSharedPtr<FJsonObject> DefaultObj = MakeShared<FJsonObject>();
				DefaultObj->SetStringField(TEXT("name"), Prop->GetName());
				DefaultObj->SetStringField(TEXT("value"), ValueText);
				ClassDefaults.Add(MakeShared<FJsonValueObject>(DefaultObj));
			}
		}

		if (const UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP))
		{
			if (AnimBP->TargetSkeleton)
			{
				TSharedPtr<FJsonObject> DefaultObj = MakeShared<FJsonObject>();
				DefaultObj->SetStringField(TEXT("name"), TEXT("TargetSkeleton"));
				DefaultObj->SetStringField(TEXT("value"), AnimBP->TargetSkeleton->GetPathName());
				ClassDefaults.Add(MakeShared<FJsonValueObject>(DefaultObj));
			}

			USkeletalMesh* PreviewMesh = nullptr;
			if (UAnimBlueprint* MutableAnimBP = const_cast<UAnimBlueprint*>(AnimBP))
			{
				PreviewMesh = MutableAnimBP->GetPreviewMesh(false);
			}
			if (!PreviewMesh && AnimBP->TargetSkeleton)
			{
				PreviewMesh = AnimBP->TargetSkeleton->GetPreviewMesh(true);
			}
			if (PreviewMesh)
			{
				TSharedPtr<FJsonObject> DefaultObj = MakeShared<FJsonObject>();
				DefaultObj->SetStringField(TEXT("name"), TEXT("PreviewSkeletalMesh"));
				DefaultObj->SetStringField(TEXT("value"), PreviewMesh->GetPathName());
				ClassDefaults.Add(MakeShared<FJsonValueObject>(DefaultObj));
			}
		}
	}
	Root->SetArrayField(TEXT("class_defaults"), ClassDefaults);

	// Dispatchers (MC delegates)
	TArray<TSharedPtr<FJsonValue>> Dispatchers;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate ||
			Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
		{
			auto DObj = MakeShared<FJsonObject>();
			DObj->SetStringField(TEXT("name"), Var.VarName.ToString());
			DObj->SetArrayField(TEXT("params"), TArray<TSharedPtr<FJsonValue>>());
			Dispatchers.Add(MakeShared<FJsonValueObject>(DObj));
		}
	}
	Root->SetArrayField(TEXT("dispatchers"), Dispatchers);

	// Components
	TArray<TSharedPtr<FJsonValue>> Comps;
	if (BP->SimpleConstructionScript)
	{
		const TArray<USCS_Node*> OrderedNodes = GetSCSNodesParentFirst_ExportBpy(BP);
		for (USCS_Node* SCSNode : OrderedNodes)
		{
			if (!SCSNode) continue;
			auto CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("name"),       SCSNode->GetVariableName().ToString());
			CObj->SetStringField(TEXT("class_name"), SCSNode->ComponentClass ? SCSNode->ComponentClass->GetName() : TEXT(""));
			CObj->SetStringField(TEXT("parent"),     ResolveComponentParentName_ExportBpy(BP, SCSNode));
			const FString AttachToName = ResolveComponentAttachToName_ExportBpy(BP, SCSNode);
			if (!AttachToName.IsEmpty())
			{
				CObj->SetStringField(TEXT("attach_to_name"), AttachToName);
			}

			// Export non-default component template properties (SkeletalMesh, AnimClass, etc.)
			auto PropsObj = MakeShared<FJsonObject>();
			if (SCSNode->ComponentTemplate)
			{
				UActorComponent* Template = SCSNode->ComponentTemplate;
				const UObject* CDO = Template->GetClass()->GetDefaultObject(false);

				for (TFieldIterator<FProperty> It(Template->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
				{
					const FProperty* Prop = *It;
					if (!Prop) continue;
					if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly)) continue;
					const FString PropName = Prop->GetName();
					if (PropName.Equals(TEXT("AttachParent"), ESearchCase::IgnoreCase) ||
						PropName.Equals(TEXT("AttachSocketName"), ESearchCase::IgnoreCase))
					{
						continue;
					}
					const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Template);
					const void* DefaultPtr = CDO ? Prop->ContainerPtrToValuePtr<void>(CDO) : nullptr;
					if (DefaultPtr && Prop->Identical(ValuePtr, DefaultPtr)) continue;

					FString ExportedValue;
					if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
					{
						UObject* ObjVal = ObjProp->GetObjectPropertyValue(ValuePtr);
						if (!ObjVal) continue;
						ExportedValue = ObjVal->GetPathName();
					}
					else if (const FClassProperty* ClsProp = CastField<FClassProperty>(Prop))
					{
						UClass* ClsVal = Cast<UClass>(ClsProp->GetObjectPropertyValue(ValuePtr));
						if (!ClsVal) continue;
						ExportedValue = ClsVal->GetPathName();
					}
					else
					{
						Prop->ExportTextItem_Direct(ExportedValue, ValuePtr, DefaultPtr, Template, PPF_None);
						if (ExportedValue.IsEmpty()) continue;
					}

					PropsObj->SetStringField(Prop->GetName(), ExportedValue);
				}
			}
			CObj->SetObjectField(TEXT("properties"), PropsObj);
			Comps.Add(MakeShared<FJsonValueObject>(CObj));
		}
	}
	Root->SetArrayField(TEXT("components"), Comps);

	// Graphs
	TArray<TSharedPtr<FJsonValue>> Graphs;

	TArray<UEdGraph*> RootGraphs;
	CollectRootGraphs_ExportBpy(BP, RootGraphs);
	for (UEdGraph* G : RootGraphs)
	{
		if (G)
		{
			Graphs.Add(MakeShared<FJsonValueObject>(SerializeGraph(G)));
		}
	}

	Root->SetArrayField(TEXT("graphs"), Graphs);

	// Timelines (empty array for now — timeline data is inside K2Node_Timeline)
	Root->SetArrayField(TEXT("timelines"), TArray<TSharedPtr<FJsonValue>>());

	return Root;
}

// ─── SerializeGraph ──────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> UBPDirectExporter::SerializeGraph(UEdGraph* Graph)
{
	auto GObj = MakeShared<FJsonObject>();

	FString GraphName = Graph->GetName();
	GObj->SetStringField(TEXT("name"), GraphName);
	GObj->SetStringField(
		TEXT("graph_guid"),
		Graph->GraphGuid.IsValid() ? Graph->GraphGuid.ToString(EGuidFormats::Digits) : FString());
	GObj->SetStringField(TEXT("graph_outer"), GetGraphOuterKind_ExportBpy(Graph));

	// Determine graph type
	FString GType = TEXT("event_graph");
	if (IsBlendStackGraphLike_ExportBpy(Graph))
	{
		GType = TEXT("blend_stack");
	}
	else if (Graph->IsA<UAnimationStateMachineGraph>())
	{
		GType = TEXT("state_machine");
	}
	else if (Graph->IsA<UAnimationStateGraph>())
	{
		GType = TEXT("state");
	}
	else if (Graph->IsA<UAnimationCustomTransitionGraph>())
	{
		GType = TEXT("custom_transition");
	}
	else if (Graph->IsA<UAnimationTransitionGraph>())
	{
		GType =
			Graph->GetSchema() && Graph->GetSchema()->IsA<UAnimationConduitGraphSchema>()
				? TEXT("conduit")
				: TEXT("transition");
	}
	else if (Graph->GetOuter() && Graph->GetOuter()->IsA<UK2Node_Composite>())
	{
		GType = TEXT("composite");
	}
	else if (Graph->GetOuter() && Graph->GetOuter()->IsA<UBlueprint>())
	{
		UBlueprint* BP = Cast<UBlueprint>(Graph->GetOuter());
		GType = GetRootGraphType_ExportBpy(BP, Graph);
	}
	GObj->SetStringField(TEXT("graph_type"), GType);

	// Function inputs/outputs from FunctionEntry node
	TArray<TSharedPtr<FJsonValue>> Inputs, Outputs;
	GObj->SetBoolField(TEXT("is_pure"),   false);
	GObj->SetBoolField(TEXT("thread_safe"), false);
	GObj->SetStringField(TEXT("category"), TEXT(""));

	if (GType == TEXT("function"))
	{
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		Graph->GetNodesOfClass(EntryNodes);
		for (UK2Node_FunctionEntry* EntryNode : EntryNodes)
		{
			if (EntryNode)
			{
				GObj->SetBoolField(TEXT("is_pure"), (EntryNode->GetFunctionFlags() & FUNC_BlueprintPure) != 0);
				GObj->SetBoolField(TEXT("thread_safe"), EntryNode->MetaData.bThreadSafe);
				const FString FunctionCategory = ResolveFunctionCategory_ExportBpy(Graph, EntryNode);
				GObj->SetStringField(TEXT("category"), FunctionCategory);
				break;
			}
		}
	}
	else if (GType == TEXT("macro") || GType == TEXT("composite"))
	{
		TArray<UK2Node_Tunnel*> TunnelNodes;
		Graph->GetNodesOfClass(TunnelNodes);
		for (const UK2Node_Tunnel* TunnelNode : TunnelNodes)
		{
			if (!TunnelNode)
			{
				continue;
			}

			const bool bIsEntryTunnel = TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs;
			const bool bIsExitTunnel = TunnelNode->bCanHaveInputs && !TunnelNode->bCanHaveOutputs;
			if (!bIsEntryTunnel && !bIsExitTunnel)
			{
				continue;
			}

			TArray<TSharedPtr<FJsonValue>>& TargetArray = bIsEntryTunnel ? Inputs : Outputs;
			for (UEdGraphPin* Pin : TunnelNode->Pins)
			{
				const bool bMatchesDirection =
					(bIsEntryTunnel && Pin->Direction == EGPD_Output) ||
					(bIsExitTunnel && Pin->Direction == EGPD_Input);
				if (!bMatchesDirection || Pin->PinName.IsNone())
				{
					continue;
				}

				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinObj->SetStringField(TEXT("type"), NormalizeTypeString_ExportBpy(Pin->PinType));
				TargetArray.Add(MakeShared<FJsonValueObject>(PinObj));
			}
		}
	}

	GObj->SetArrayField(TEXT("inputs"), Inputs);
	GObj->SetArrayField(TEXT("outputs"), Outputs);

	// Nodes
	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		UK2Node* K2 = Cast<UK2Node>(N);
		if (K2)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(SerializeNode(K2)));
			continue;
		}

		if (IsSupportedNonK2GraphNode_ExportBpy(N))
		{
			Nodes.Add(MakeShared<FJsonValueObject>(SerializeGenericNode(N)));
		}
	}
	GObj->SetArrayField(TEXT("nodes"), Nodes);

	// Connections
	GObj->SetArrayField(TEXT("connections"), SerializeConnections(Graph));

	return GObj;
}

// ─── SerializeNode ───────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> UBPDirectExporter::SerializeNode(UK2Node* Node)
{
	auto NObj = MakeShared<FJsonObject>();
	const FNodeInfo Info = BuildNodeInfo_ExportBpy(Node);

	NObj->SetStringField(TEXT("uid"),        Node->NodeGuid.ToString());
	NObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	NObj->SetNumberField(TEXT("pos_x"),      Node->NodePosX);
	NObj->SetNumberField(TEXT("pos_y"),      Node->NodePosY);
	NObj->SetStringField(TEXT("node_guid"),  Info.NodeGuid);

	// member_name / function_ref
	FString MemberName, FunctionRef, TargetType;

	if (auto* CE = Cast<UK2Node_CustomEvent>(Node))
		MemberName = CE->CustomFunctionName.ToString();
	else if (auto* Evt = Cast<UK2Node_Event>(Node))
		MemberName = Evt->EventReference.GetMemberName().ToString();
	else if (auto* Msg = Cast<UK2Node_Message>(Node))
	{
		const FString FuncName = Msg->FunctionReference.GetMemberName().ToString();
		if (UClass* C = Msg->FunctionReference.GetMemberParentClass())
		{
			FunctionRef = C->GetPathName() + TEXT("::") + FuncName;
		}
		else
		{
			FunctionRef = FuncName;
		}
	}
	else if (auto* Fn = Cast<UK2Node_CallFunction>(Node))
	{
		FString FuncName = Fn->FunctionReference.GetMemberName().ToString();
		if (!Fn->FunctionReference.IsSelfContext())
		{
			if (UClass* C = Fn->FunctionReference.GetMemberParentClass())
			{
				FunctionRef = C->GetPathName() + TEXT("::") + FuncName;
			}
			else
			{
				FunctionRef = FuncName;
			}
		}
		else
		{
			FunctionRef = FuncName;
		}
	}
	else if (auto* VG = Cast<UK2Node_VariableGet>(Node))
		MemberName = VG->VariableReference.GetMemberName().ToString();
	else if (auto* VS = Cast<UK2Node_VariableSet>(Node))
		MemberName = VS->VariableReference.GetMemberName().ToString();
	else if (auto* FE = Cast<UK2Node_FunctionEntry>(Node))
		MemberName = FE->CustomGeneratedFunctionName.ToString();
	else if (auto* DC = Cast<UK2Node_DynamicCast>(Node))
	{
		if (DC->TargetType)
			TargetType = DC->TargetType->GetPathName();
	}
	else if (auto* MI = Cast<UK2Node_MacroInstance>(Node))
	{
		if (UEdGraph* MG = MI->GetMacroGraph())
			MemberName = MG->GetName();
	}
	else if (auto* DelegateNode = Cast<UK2Node_BaseMCDelegate>(Node))
	{
		MemberName = DelegateNode->GetPropertyName().ToString();
	}
	else if (auto* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
	{
		MemberName = CreateDelegateNode->GetFunctionName().ToString();
	}
	else if (auto* TL = Cast<UK2Node_Timeline>(Node))
		MemberName = TL->TimelineName.ToString();

	NObj->SetStringField(TEXT("member_name"),  MemberName);
	NObj->SetStringField(TEXT("function_ref"), FunctionRef);
	NObj->SetStringField(TEXT("target_type"),  TargetType);
	FString TunnelType;
	if (const UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(Node))
	{
		if (TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs)
		{
			TunnelType = TEXT("entry");
		}
		else if (TunnelNode->bCanHaveInputs && !TunnelNode->bCanHaveOutputs)
		{
			TunnelType = TEXT("exit");
		}
	}
	NObj->SetStringField(TEXT("tunnel_type"), TunnelType);
	TArray<TSharedPtr<FJsonValue>> CustomParamsArray;
	for (const TPair<FString, FString>& Param : Info.CustomParams)
	{
		TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
		ParamObj->SetStringField(TEXT("name"), Param.Key);
		ParamObj->SetStringField(TEXT("type"), Param.Value);
		CustomParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
	}
	NObj->SetArrayField(TEXT("custom_params"), CustomParamsArray);

	// Defaults (unconnected input pins with non-empty default)
	auto DefaultsObj = MakeShared<FJsonObject>();
	auto InputPinTypesObj = MakeShared<FJsonObject>();
	auto OutputPinTypesObj = MakeShared<FJsonObject>();
	for (UEdGraphPin* Pin : Node->Pins)
	{
		const FString PinName = Pin->PinName.ToString();
		const FString PinTypeString = NormalizeTypeString_ExportBpy(Pin->PinType);
		if (Pin->Direction == EGPD_Input)
		{
			InputPinTypesObj->SetStringField(PinName, PinTypeString);
		}
		else if (Pin->Direction == EGPD_Output)
		{
			OutputPinTypesObj->SetStringField(PinName, PinTypeString);
		}

		const FString PinDefaultValue = GetPinDefaultValueForExport_ExportBpy(Pin);
		if (!PinDefaultValue.IsEmpty())
		{
			DefaultsObj->SetStringField(PinName, PinDefaultValue);
		}
	}
	NObj->SetObjectField(TEXT("defaults"), DefaultsObj);
	NObj->SetObjectField(TEXT("input_pin_types"), InputPinTypesObj);
	NObj->SetObjectField(TEXT("output_pin_types"), OutputPinTypesObj);

	auto NodePropsObj = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Entry : Info.NodeProps)
	{
		NodePropsObj->SetStringField(Entry.Key, Entry.Value);
	}
	if (const UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
	{
		if (CompositeNode->BoundGraph)
		{
			NodePropsObj->SetStringField(
				TEXT("BoundGraphJson"),
				SerializeJsonCompact_ExportBpy(SerializeGraph(CompositeNode->BoundGraph)));
		}
	}
	NObj->SetObjectField(TEXT("node_props"), NodePropsObj);

	auto PinAliasesObj = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Entry : Info.PinAliases)
	{
		PinAliasesObj->SetStringField(Entry.Key, Entry.Value);
	}
	NObj->SetObjectField(TEXT("pin_aliases"), PinAliasesObj);

	auto PinIdsObj = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Entry : Info.PinIds)
	{
		PinIdsObj->SetStringField(Entry.Key, Entry.Value);
	}
	NObj->SetObjectField(TEXT("pin_ids"), PinIdsObj);

	return NObj;
}

TSharedPtr<FJsonObject> UBPDirectExporter::SerializeGenericNode(UEdGraphNode* Node)
{
	auto NObj = MakeShared<FJsonObject>();
	if (!Node)
	{
		return NObj;
	}

	NObj->SetStringField(TEXT("uid"), Node->NodeGuid.ToString());
	NObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	NObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
	NObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
	NObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::Digits));

	auto NodeProps = MakeShared<FJsonObject>();
	auto AddGraphJsonProp = [&NodeProps](const TCHAR* FieldName, UEdGraph* ChildGraph)
	{
		if (FieldName && ChildGraph)
		{
			NodeProps->SetStringField(FieldName, SerializeJsonCompact_ExportBpy(SerializeGraph(ChildGraph)));
		}
	};

	if (const UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
	{
		AddGraphJsonProp(TEXT("BoundGraphJson"), StateNode->BoundGraph);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("StateType"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("StateEntered"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("StateLeft"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("StateFullyBlended"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("bAlwaysResetOnEntry"), true);

		// UE may keep state-entry hook on StateResult while StateEntered.NotifyName is left blank.
		// Export a stable StateEntered value with NotifyName so roundtrip import does not lose bindings.
		FString ExistingStateEnteredText;
		NodeProps->TryGetStringField(TEXT("StateEntered"), ExistingStateEnteredText);
		const FString ExistingNotifyName =
			ExtractStateNotifyNameFromStateEnteredText_ExportBpy(ExistingStateEnteredText);

		TSet<FName> BlueprintFunctionNames;
		UBlueprint* OwnerBlueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
		if (OwnerBlueprint)
		{
			for (UEdGraph* FunctionGraph : OwnerBlueprint->FunctionGraphs)
			{
				if (FunctionGraph)
				{
					BlueprintFunctionNames.Add(FName(*FunctionGraph->GetName()));
				}
			}
		}

		auto IsKnownFunction = [&BlueprintFunctionNames](const FString& FunctionName) -> bool
		{
			return !FunctionName.IsEmpty() && BlueprintFunctionNames.Contains(FName(*FunctionName));
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

		FString DesiredNotifyName;
		if (StateNode->BoundGraph)
		{
			for (UEdGraphNode* BoundNode : StateNode->BoundGraph->Nodes)
			{
				if (const UAnimGraphNode_StateResult* StateResultNode = Cast<UAnimGraphNode_StateResult>(BoundNode))
				{
					const FString StateResultNodeText =
						ReadNodePropertyAsText_ExportBpy(StateResultNode, TEXT("Node"));
					DesiredNotifyName = ExtractStateResultHookFunctionName_ExportBpy(
						StateResultNodeText,
						TEXT("StateEntryFunction"));
					if (!DesiredNotifyName.IsEmpty())
					{
						break;
					}
				}
			}
		}

		if (!IsKnownFunction(DesiredNotifyName))
		{
			if (IsKnownFunction(ExistingNotifyName))
			{
				DesiredNotifyName = ExistingNotifyName;
			}
			else
			{
				const FString StateTitle = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
				const FString StateGraphName = StateNode->BoundGraph ? StateNode->BoundGraph->GetName() : FString();
				const FString StateTokenSource = !StateTitle.IsEmpty() ? StateTitle : StateGraphName;
				const FString StateCamel = BuildStateCamelToken(StateTokenSource);
				const FString StateKey = NormalizeKey(StateTokenSource);
				if (!StateCamel.IsEmpty())
				{
					const FString ExactCandidate = FString::Printf(TEXT("OnStateEntry_%s"), *StateCamel);
					if (IsKnownFunction(ExactCandidate))
					{
						DesiredNotifyName = ExactCandidate;
					}
				}

				if (DesiredNotifyName.IsEmpty() && !StateKey.IsEmpty())
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
							DesiredNotifyName = Candidate;
							break;
						}
					}
				}
			}
		}

		FGuid DesiredNotifyGuid;
		if (!DesiredNotifyName.IsEmpty() && OwnerBlueprint)
		{
			if (const UClass* SkeletonClass = OwnerBlueprint->SkeletonGeneratedClass)
			{
				FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
					SkeletonClass,
					FName(*DesiredNotifyName),
					DesiredNotifyGuid);
			}
			if (!DesiredNotifyGuid.IsValid())
			{
				for (UEdGraph* FunctionGraph : OwnerBlueprint->FunctionGraphs)
				{
					if (FunctionGraph && FunctionGraph->GetName() == DesiredNotifyName)
					{
						DesiredNotifyGuid = FunctionGraph->GraphGuid;
						break;
					}
				}
			}
		}

		FString PatchedStateEnteredText = BuildStateEnteredText_ExportBpy(
			DesiredNotifyName,
			DesiredNotifyGuid);
		if (!PatchedStateEnteredText.IsEmpty())
		{
			NodeProps->SetStringField(TEXT("StateEntered"), PatchedStateEnteredText);
		}
	}

	if (const UAnimStateConduitNode* ConduitNode = Cast<UAnimStateConduitNode>(Node))
	{
		AddGraphJsonProp(TEXT("BoundGraphJson"), ConduitNode->BoundGraph);
	}

	if (const UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node))
	{
		AddGraphJsonProp(TEXT("BoundGraphJson"), TransitionNode->GetBoundGraph());
		AddGraphJsonProp(TEXT("CustomTransitionGraphJson"), TransitionNode->GetCustomTransitionGraph());
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("PriorityOrder"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("CrossfadeDuration"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("BlendMode"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("CustomBlendCurve"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("BlendProfileWrapper"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("bAutomaticRuleBasedOnSequencePlayerInState"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("AutomaticRuleTriggerTime"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("MinTimeBeforeReentry"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("SyncGroupNameToRequireValidMarkersRule"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("LogicType"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("TransitionStart"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("TransitionEnd"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("TransitionInterrupt"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("bAllowInertializationForSelfTransitions"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("Bidirectional"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("bDisabled"), true);
	}

	if (const UAnimStateAliasNode* AliasNode = Cast<UAnimStateAliasNode>(Node))
	{
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("bGlobalAlias"), true);
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("StateAliasName"));

		TArray<FString> AliasedStateUids;
		for (const TWeakObjectPtr<UAnimStateNodeBase>& AliasedState : AliasNode->GetAliasedStates())
		{
			if (const UAnimStateNodeBase* AliasedStateNode = AliasedState.Get())
			{
				AliasedStateUids.Add(AliasedStateNode->NodeGuid.ToString());
			}
		}

		if (AliasedStateUids.Num() > 0)
		{
			NodeProps->SetStringField(TEXT("AliasedStateUids"), FString::Join(AliasedStateUids, TEXT("|")));
		}
	}

	if (const UAnimGraphNode_LinkedAnimLayer* LinkedLayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
	{
		NodeProps->SetStringField(TEXT("LinkedAnimLayerLayer"), LinkedLayerNode->Node.Layer.ToString());
		UClass* InterfaceClass = ResolveLinkedAnimLayerInterfaceClass_ExportBpy(LinkedLayerNode);

		if (InterfaceClass)
		{
			NodeProps->SetStringField(TEXT("LinkedAnimLayerInterfaceClass"), InterfaceClass->GetPathName());
		}
		else
		{
			NodeProps->SetStringField(TEXT("LinkedAnimLayerInterfaceClass"), TEXT("None"));
		}
	}

	if (Node->IsA<UEdGraphNode_Comment>())
	{
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("NodeComment"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("CommentColor"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("FontSize"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("MoveMode"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("CommentDepth"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("bColorCommentBubble"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("bCommentBubbleVisible"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("NodeWidth"));
		AddGenericNodePropertyText_ExportBpy(Node, NodeProps, TEXT("NodeHeight"));
	}

	if (NodeProps->Values.Num() > 0)
	{
		NObj->SetObjectField(TEXT("node_props"), NodeProps);
	}

	auto PinIdsObj = MakeShared<FJsonObject>();
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			PinIdsObj->SetStringField(Pin->PinName.ToString(), Pin->PinId.ToString(EGuidFormats::Digits));
		}
	}
	if (PinIdsObj->Values.Num() > 0)
	{
		NObj->SetObjectField(TEXT("pin_ids"), PinIdsObj);
	}

	return NObj;
}

// ─── SerializeConnections ─────────────────────────────────────────────────────

TArray<TSharedPtr<FJsonValue>> UBPDirectExporter::SerializeConnections(UEdGraph* Graph)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	TSet<FString> SeenConnections;

	// Build uid map (include knots so reroute chains can be reconstructed exactly).
	TMap<UEdGraphNode*, FString> NodeUidMap;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!N)
		{
			continue;
		}
		NodeUidMap.Add(N, N->NodeGuid.ToString());
	}

	auto AppendConnection = [&](UEdGraphNode* SrcNode, UEdGraphPin* SrcPin, UEdGraphPin* RawDst)
	{
		FString* SrcUid = NodeUidMap.Find(SrcNode);
		if (!SrcUid || !SrcPin || !RawDst)
		{
			return;
		}

		TArray<UEdGraphPin*> DestinationPins;
		const bool bSrcIsKnot = SrcNode->IsA<UK2Node_Knot>();
		const bool bDstIsKnot = RawDst->GetOwningNode() && RawDst->GetOwningNode()->IsA<UK2Node_Knot>();
		if (bSrcIsKnot || bDstIsKnot)
		{
			DestinationPins.Add(RawDst);
		}
		else
		{
			ResolveRerouteChainAll(RawDst, DestinationPins);
		}

		for (UEdGraphPin* DstPin : DestinationPins)
		{
			if (!DstPin)
			{
				continue;
			}

			FString* DstUid = NodeUidMap.Find(DstPin->GetOwningNode());
			if (!DstUid)
			{
				continue;
			}

			const FString SrcPinName = SrcPin->PinName.ToString();
			const FString DstPinName = DstPin->PinName.ToString();
			const FString DedupKey = *SrcUid + TEXT("|") + SrcPinName + TEXT("|") + *DstUid + TEXT("|") + DstPinName;
			if (SeenConnections.Contains(DedupKey))
			{
				continue;
			}
			SeenConnections.Add(DedupKey);

			auto CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("src_node"), *SrcUid);
			CObj->SetStringField(TEXT("src_pin"),  SrcPinName);
			CObj->SetStringField(TEXT("dst_node"), *DstUid);
			CObj->SetStringField(TEXT("dst_pin"),  DstPinName);
			Result.Add(MakeShared<FJsonValueObject>(CObj));
		}
	};

	for (UEdGraphNode* N : Graph->Nodes)
	{
		FString* SrcUid = NodeUidMap.Find(N);
		if (!SrcUid)
		{
			continue;
		}

		for (UEdGraphPin* SrcPin : N->Pins)
		{
			if (!SrcPin || SrcPin->Direction != EGPD_Output)
			{
				continue;
			}

			for (UEdGraphPin* RawDst : SrcPin->LinkedTo)
			{
				AppendConnection(N, SrcPin, RawDst);
			}

			if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				const UK2Node* SrcNode = Cast<UK2Node>(N);
				if (!SrcNode)
				{
					continue;
				}

				for (UEdGraphPin* SyntheticDst : GetSyntheticExecPassThroughTargets_ExportBpy(const_cast<UK2Node*>(SrcNode), SrcPin))
				{
					AppendConnection(N, SrcPin, SyntheticDst);
				}
			}
		}
	}

	return Result;
}

