// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "BPDirectImporter.h"

#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Timeline.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Message.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Select.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Self.h"
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
#include "UObject/UObjectIterator.h"
#include "StructUtils/InstancedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
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

	return nullptr;
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

	// `VariableGetIsPure=false` inside meta is not sufficient on its own.
	// When users patch `.bp.py` by hand, the code can intentionally collapse a
	// validated get back to a plain `g.get_var(...)` while the stale meta still
	// says "impure". Recreating the validated get in that case manufactures an
	// exec pin with no execution flow and produces the compiler warning:
	// "Get was pruned because its Exec pin is not connected".
	//
	// Treat the graph DSL / compiled JSON as the source of truth for node shape.
	// We only restore an impure get when the imported node explicitly exposes
	// exec-like pins (`execute` / `then` / `else`), which covers real validated
	// get and branch variations while ignoring stale metadata.

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
	if (!bHasResultType && !bHasOutputClass && !bHasFallbackAsset && !bHasResultAssets)
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

	if (bHasFallbackAsset)
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

	if (bHasResultAssets)
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
void ParsePinTypeString_ImportBpy(const FString& TypeStr, FEdGraphPinType& OutType);
void ParseGraphPins_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson, const TCHAR* FieldName, TArray<TPair<FString, FEdGraphPinType>>& OutPins);

int32 GetGraphImportPriority_ImportBpy(const TSharedPtr<FJsonObject>& GraphJson)
{
	if (!GraphJson.IsValid())
	{
		return MAX_int32;
	}

	const FString GraphType = GraphJson->GetStringField(TEXT("graph_type"));
	if (GraphType == TEXT("function"))
	{
		return 0;
	}
	if (GraphType == TEXT("macro"))
	{
		return 1;
	}
	if (GraphType == TEXT("event_graph"))
	{
		return 2;
	}
	return 3;
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

	if (IsAnimBlueprintFunctionGraph_ImportBpy(BP, nullptr, OutGraphType, OutGraphName))
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
			OutGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP, FName(*OutGraphName),
				UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, OutGraph, false, nullptr);
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
		!IsAnimBlueprintFunctionGraph_ImportBpy(BP, OutGraph, OutGraphType, OutGraphName);

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

	FString Category;
	FString Sub;
	if (!TypeStr.Split(TEXT("/"), &Category, &Sub))
	{
		OutType.PinCategory = FName(*TypeStr);
		return;
	}

	OutType.PinCategory = FName(*Category);

	UObject* SubObject = nullptr;
	if (OutType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		SubObject = ResolveNamedObject_ImportBpy<UScriptStruct>(Sub);
		if (!SubObject)
		{
			SubObject = ResolveCommonStructType_ImportBpy(Sub);
		}
	}
	else if (OutType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		SubObject = ResolveNamedObject_ImportBpy<UEnum>(Sub);
	}
	else if (OutType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_Interface ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		OutType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		SubObject = ResolveNamedObject_ImportBpy<UClass>(Sub);
	}
	else if (OutType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		OutType.PinSubCategory = FName(*Sub);
		return;
	}
	else
	{
		SubObject = ResolveNamedObject_ImportBpy<UObject>(Sub);
	}

	if (SubObject)
	{
		OutType.PinSubCategoryObject = SubObject;
	}
	else if (!Sub.IsEmpty())
	{
		OutType.PinSubCategory = FName(*Sub);
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
		if (RequestedPinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase))
		{
			return TEXT("exec");
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

void EnsureDynamicPinsForRequest_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
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
}

UEdGraphPin* FindExistingPinFlexible_ImportBpy(UEdGraphNode* Node, const FString& RequestedPinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	const FString NormalizedRequested = NormalizeRequestedPinName_ImportBpy(Node, RequestedPinName);
	if (UEdGraphPin* ExactPin = Node->FindPin(FName(*NormalizedRequested), Direction))
	{
		return ExactPin;
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
		if (StripGuidSuffix_ImportBpy(PinName).Equals(RequestedNoGuid, ESearchCase::IgnoreCase))
		{
			return Pin;
		}

		const FString FriendlyName = Pin->PinFriendlyName.ToString();
		if (!FriendlyName.IsEmpty() &&
			(FriendlyName.Equals(NormalizedRequested, ESearchCase::IgnoreCase) ||
			 FriendlyName.Equals(RequestedNoGuid, ESearchCase::IgnoreCase)))
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
	FEdGraphPinType SelectIndexPinType;

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
				if (IndexContainer == TEXT("array"))
				{
					SelectIndexPinType.ContainerType = EPinContainerType::Array;
				}
				else if (IndexContainer == TEXT("set"))
				{
					SelectIndexPinType.ContainerType = EPinContainerType::Set;
				}
				else if (IndexContainer == TEXT("map"))
				{
					SelectIndexPinType.ContainerType = EPinContainerType::Map;
				}
			}

			bApplySelectIndexTypePostReconstruct = true;
			bNeedsReconstruct = true;
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
		}

		if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
		{
			if (Key == TEXT("MacroGraph"))
			{
				if (UEdGraph* MacroGraph = ResolveMacroGraph_ImportBpy(JsonValue->AsString(), FString()))
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
			if (Key == TEXT("Enum") || Key == TEXT("IndexType") || Key == TEXT("IndexContainer"))
			{
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

	if (bApplySelectIndexTypePostReconstruct)
	{
		if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
		{
			if (UEdGraphPin* IndexPin = SelectNode->GetIndexPin())
			{
				IndexPin->PinType = SelectIndexPinType;
				SelectNode->PinTypeChanged(IndexPin);
			}
		}
	}

	return true;
}

bool ApplyPinDefaults_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson, FString& OutError)
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
		const FString RequestedPinName = ResolveNodePinAlias_ImportBpy(NodeJson, Entry.Key);
		if (UEdGraphPin* Pin = FindPinFlexible_ImportBpy(Node, RequestedPinName, EGPD_Input))
		{
			ApplyDefaultToPin_ImportBpy(Pin, Entry.Value);
		}
		else
		{
			OutError = FString::Printf(
				TEXT("Cannot resolve input pin '%s' while applying default on node %s"),
				*RequestedPinName,
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
		const FString RequestedPinName = ResolveNodePinAlias_ImportBpy(NodeJson, Entry.Key);
		UEdGraphPin* Pin = FindPinFlexible_ImportBpy(Node, RequestedPinName, EGPD_Input);
		if (!Pin)
		{
			Pin = FindPinFlexible_ImportBpy(Node, RequestedPinName, EGPD_Output);
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
					*RequestedPinName,
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
				*RequestedPinName,
				*DescribeNode_ImportBpy(Node));
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
			Node->NodeGuid = ParsedGuid;
		}
	}

	if (!ApplyNodeProps_ImportBpy(Node, NodeJson, OutError))
	{
		return false;
	}
	if (!ApplyPinDefaults_ImportBpy(Node, NodeJson, OutError))
	{
		return false;
	}
	if (!ApplyPinIds_ImportBpy(Node, NodeJson, OutError))
	{
		return false;
	}

	return true;
}

void EnsureFunctionPins_ImportBpy(UK2Node_FunctionEntry* EntryNode, const TArray<TPair<FString, FEdGraphPinType>>& Inputs)
{
	if (!EntryNode)
	{
		return;
	}

	for (const TPair<FString, FEdGraphPinType>& Input : Inputs)
	{
		if (!FindPinFlexible_ImportBpy(EntryNode, Input.Key, EGPD_Output))
		{
			EntryNode->CreateUserDefinedPin(FName(*Input.Key), Input.Value, EGPD_Output, false);
		}
	}
}

void EnsureFunctionPins_ImportBpy(UK2Node_FunctionResult* ResultNode, const TArray<TPair<FString, FEdGraphPinType>>& Outputs)
{
	if (!ResultNode)
	{
		return;
	}

	for (const TPair<FString, FEdGraphPinType>& Output : Outputs)
	{
		if (!FindPinFlexible_ImportBpy(ResultNode, Output.Key, EGPD_Input))
		{
			ResultNode->CreateUserDefinedPin(FName(*Output.Key), Output.Value, EGPD_Input, false);
		}
	}
}

void ClearGraphNodes_ImportBpy(UBlueprint* BP, UEdGraph* Graph)
{
	if (!BP || !Graph)
	{
		return;
	}

	const bool bIsFunctionGraph =
		BP->FunctionGraphs.Contains(Graph) &&
		!IsAnimBlueprintFunctionGraph_ImportBpy(BP, Graph, TEXT("function"), Graph->GetName());
	const bool bIsMacroGraph = BP->MacroGraphs.Contains(Graph);

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
		if (bIsMacroGraph && Node->IsA<UK2Node_Tunnel>())
		{
			continue;
		}

		FBlueprintEditorUtils::RemoveNode(BP, Node, true);
	}
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

	const FName VariableFName(*VarName);

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
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		BP->MarkPackageDirty();
	}
}

// ─── CreateGraph ─────────────────────────────────────────────────────────────

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

	const bool bTreatAsRegularFunctionGraph =
		(GraphType == TEXT("function")) &&
		!IsAnimBlueprintFunctionGraph_ImportBpy(BP, Graph, GraphType, GraphName);

	// Import is authoritative for a graph. Clear pre-existing/default nodes first so
	// re-imports do not accumulate stale nodes such as the template Event Tick.
	ClearGraphNodes_ImportBpy(BP, Graph);

	TArray<TPair<FString, FEdGraphPinType>> GraphInputs;
	TArray<TPair<FString, FEdGraphPinType>> GraphOutputs;
	auto ParsePinArray = [](const TSharedPtr<FJsonObject>& InGraphJson, const TCHAR* FieldName, TArray<TPair<FString, FEdGraphPinType>>& OutPins)
	{
		const TArray<TSharedPtr<FJsonValue>>* PinsArray = nullptr;
		if (!InGraphJson->TryGetArrayField(FieldName, PinsArray))
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
			UBPDirectImporter::ParsePinType(PinTypeString, PinType);
			OutPins.Add(TPair<FString, FEdGraphPinType>(PinName, PinType));
		}
	};

	if (bTreatAsRegularFunctionGraph)
	{
		ParsePinArray(GraphJson, TEXT("inputs"), GraphInputs);
		ParsePinArray(GraphJson, TEXT("outputs"), GraphOutputs);

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

	// Create nodes
	const TArray<TSharedPtr<FJsonValue>>* NodesArr;
	TMap<FString, UEdGraphNode*> NodeMap; // uid → node
	int32 ReusableResultNodeIndex = 0;
	TArray<UK2Node_FunctionResult*> ExistingResultNodes;
	if (bTreatAsRegularFunctionGraph)
	{
		Graph->GetNodesOfClass(ExistingResultNodes);
	}

	if (GraphJson->TryGetArrayField(TEXT("nodes"), NodesArr))
	{
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
				UEdGraphNode* Node = CreateNode(Graph, NodeObj, OutError);
				if (Node)
				{
					NodeMap.Add(Uid, Node);
				}
			}

			if (NodeMap.Num() > 0)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			}
		}

		for (auto& NJ : *NodesArr)
		{
			TSharedPtr<FJsonObject> NodeObj = NJ->AsObject();
			if (!NodeObj.IsValid())
			{
				continue;
			}
			FString Uid = NodeObj->GetStringField(TEXT("uid"));
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
				NodeMap.Add(Uid, Node);
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

	BreakAllGraphLinks_ImportBpy(Graph);

	// Create connections
	const TArray<TSharedPtr<FJsonValue>>* ConnsArr;
	if (GraphJson->TryGetArrayField(TEXT("connections"), ConnsArr))
	{
		for (auto& CJ : *ConnsArr)
		{
			TSharedPtr<FJsonObject> ConnObj = CJ->AsObject();
			FString SrcUid  = ConnObj->GetStringField(TEXT("src_node"));
			FString SrcPin  = ConnObj->GetStringField(TEXT("src_pin"));
			FString DstUid  = ConnObj->GetStringField(TEXT("dst_node"));
			FString DstPin  = ConnObj->GetStringField(TEXT("dst_pin"));
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

			if (!ConnectPins(*SrcNodePtr, SrcPin, SrcPinFull, SrcPinId, *DstNodePtr, DstPin, DstPinFull, DstPinId, OutError))
			{
				return false;
			}
		}
	}

	// Some nodes (notably promotable operator nodes) can lose input defaults once
	// wildcard typing settles during connection creation. Re-apply defaults after
	// all links exist so the imported graph retains authored constant values.
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

			if (!ApplyPinDefaults_ImportBpy(*ExistingNode, NodeObj, OutError))
			{
				return false;
			}
		}
	}

	return true;
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
	// ── Macro Instance ───────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_MacroInstance"))
	{
		Result = CreateMacroInstanceNode(Graph, TargetType, MemberName, OutError);
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
		if (UClass* GenericNodeClass = ResolveNodeClass_ImportBpy(NodeClass))
		{
			if (GenericNodeClass->IsChildOf(UEdGraphNode::StaticClass()))
			{
				UEdGraphNode* GenericNode = NewObject<UEdGraphNode>(Graph, GenericNodeClass);
				GenericNode->CreateNewGuid();
				GenericNode->PostPlacedNewNode();
				GenericNode->AllocateDefaultPins();
				Graph->AddNode(GenericNode, false, false);
				Result = GenericNode;
			}
		}
		if (!Result)
		{
			UE_LOG(LogTemp, Warning, TEXT("BPDirectImporter: unknown node class '%s', skipping"), *NodeClass);
			return nullptr;
		}
	}

	if (!Result) return nullptr;

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
	if (Func)
	{
		Node->SetFromFunction(Func);
	}
	else
	{
		if (bSelfContextCall)
		{
			// Keep unresolved bare calls as self members so later reconstruct/compile
			// can bind them against the current blueprint rather than some globally
			// discovered function on a different generated class.
			Node->FunctionReference.SetSelfMember(FName(*FuncName));
		}
		else
		{
			Node->FunctionReference.SetExternalMember(
				FName(*FuncName),
				ExplicitOwnerClass ? ExplicitOwnerClass : UObject::StaticClass());
		}
	}

	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	Graph->AddNode(Node, false, false);
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
	UEdGraph* MacroGraph = ResolveMacroGraph_ImportBpy(MacroGraphPath, MacroName);
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

	if (!CanSafelyOverwritePackageFile_ImportBpy(PackageFileName, OutError))
	{
		return false;
	}

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

	if (!CanSafelyOverwritePackageFile_ImportBpy(PackageFileName, OutError))
	{
		return false;
	}

	if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save asset package: %s"), *PackageFileName);
		return false;
	}

	return true;
}
