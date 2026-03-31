// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "BPDirectExporter.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
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
#include "K2Node_DynamicCast.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_Message.h"
#include "K2Node_Select.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Self.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionTerminator.h"
#include "Components/SceneComponent.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "InputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
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
#include "EditorAssetLibrary.h"

namespace
{
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
	GetObjectsWithOuter(Asset, Subobjects, false);
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
	Meta->SetStringField(TEXT("export_type"), TEXT("generic_object"));
	Meta->SetStringField(TEXT("outer"), Asset->GetOuter() ? Asset->GetOuter()->GetPathName() : TEXT(""));
	Meta->SetStringField(TEXT("package"), Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : TEXT(""));
	TSharedPtr<FJsonObject> PropertiesJson =
		SerializeObjectProperties_ExportBpy(Asset, Asset->GetClass()->GetDefaultObject(false));
	AppendInputActionStandaloneProperties_ExportBpy(Asset, PropertiesJson);
	Meta->SetObjectField(
		TEXT("properties"),
		PropertiesJson);
	Meta->SetArrayField(TEXT("subobjects"), SerializeStandaloneSubobjects_ExportBpy(Asset));
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

FString NormalizeTypeString_ExportBpy(const FEdGraphPinType& PinType)
{
	FString TypeStr = PinType.PinCategory.ToString();
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeStr += TEXT("/") + PinType.PinSubCategoryObject->GetName();
	}
	else if (!PinType.PinSubCategory.IsNone())
	{
		TypeStr += TEXT("/") + PinType.PinSubCategory.ToString();
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
	FString DefaultValue = Var.DefaultValue;
	if (!BP)
	{
		return DefaultValue;
	}

	UClass* GeneratedClass = BP->GeneratedClass;
	UObject* GeneratedCDO = GeneratedClass ? GeneratedClass->GetDefaultObject(false) : nullptr;
	if (!GeneratedCDO)
	{
		return DefaultValue;
	}

	const FProperty* TargetProperty = FindFProperty<FProperty>(GeneratedCDO->GetClass(), Var.VarName);
	if (!TargetProperty)
	{
		return DefaultValue;
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
		return ExportedValue;
	}

	return DefaultValue;
}

FString GetPinDefaultValue_ExportBpy(const UEdGraphPin* Pin)
{
	if (!Pin ||
		Pin->Direction != EGPD_Input ||
		!Pin->LinkedTo.IsEmpty() ||
		Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
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

	double ParsedNumber = 0.0;
	if (!Trimmed.Contains(TEXT(",")) && LexTryParseString(ParsedNumber, *Trimmed))
	{
		return Trimmed;
	}

	return MakePythonStringLiteral_ExportBpy(Trimmed);
}

FString BuildModuleName_ExportBpy(const FString& Prefix, const FString& GraphName)
{
	return Prefix + SanitizePythonIdentifier_ExportBpy(GraphName, TEXT("graph"));
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
	}
	else if (const UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(Node))
	{
		Info.FunctionName = VG->VariableReference.GetMemberName().ToString();
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

	if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		if (CastNode->TargetType)
		{
			Info.TargetType = CastNode->TargetType->GetPathName();
			Info.NodeProps.Add(TEXT("TargetType"), CastNode->TargetType->GetPathName());
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
	}

	if (const FObjectPropertyBase* InputActionProperty = FindFProperty<FObjectPropertyBase>(Node->GetClass(), FName(TEXT("InputAction"))))
	{
		if (UObject* InputActionObject = InputActionProperty->GetObjectPropertyValue_InContainer(Node))
		{
			Info.NodeProps.Add(TEXT("InputAction"), InputActionObject->GetPathName());
		}
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		const FString RawPinName = Pin->PinName.ToString();
		const FString LogicalPinName = GetLogicalPinName_ExportBpy(Node, Pin);

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

		const FString PinDefaultValue = GetPinDefaultValue_ExportBpy(Pin);
		if (!PinDefaultValue.IsEmpty())
		{
			Info.DefaultValues.Add(LogicalPinName.IsEmpty() ? RawPinName : LogicalPinName, PinDefaultValue);
		}
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

	const FString BPName = FPaths::GetBaseFilename(BlueprintPath);
	const FString BPOutDir = FPaths::Combine(OutputDir, BPName);
	IFileManager::Get().MakeDirectory(*BPOutDir, true);

	auto AppendConnectionSections = [&](const TArray<UK2Node*>& SourceNodes, const TArray<FNodeInfo>& NodeInfos, const TMap<UK2Node*, FString>& NodeVarMap, FString& InOutLines)
	{
		TArray<FString> DataLines;
		TArray<FString> ExecLines;

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
					UEdGraphPin* DstPin = ResolveRerouteChain(RawDst);
					if (!DstPin)
					{
						continue;
					}

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

					if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						ExecLines.Add(ConnLine);
					}
					else
					{
						DataLines.Add(ConnLine);
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

	auto WriteGraphs = [&](const TArray<UEdGraph*>& Graphs, const FString& Prefix) -> bool
	{
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}

			FString ModuleName;
			if (!GenerateGraphFile(BP, Graph, Prefix, BPOutDir, ModuleName, OutError))
			{
				return false;
			}

			GraphModules.Add(ModuleName);
		}
		return true;
	};

	if (!WriteGraphs(BP->FunctionGraphs, TEXT("fn_")))
	{
		return false;
	}
	if (!WriteGraphs(BP->MacroGraphs, TEXT("macro_")))
	{
		return false;
	}
	if (!WriteGraphs(BP->UbergraphPages, TEXT("evt_")))
	{
		return false;
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

	const TSharedPtr<FJsonObject> Root = SerializeBlueprintToJson(BP);
	if (!Root.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to serialize blueprint: %s"), *BlueprintPath);
		return false;
	}

	const FString PrettyJson = SerializeJsonPretty_ExportBpy(Root);
	const FString BlueprintAssetPath = GetJsonStringField_ExportBpy(Root, TEXT("path"), BP->GetPathName());
	const FString Parent = GetJsonStringField_ExportBpy(Root, TEXT("parent"), BP->ParentClass ? BP->ParentClass->GetPathName() : TEXT(""));
	const FString BlueprintType = GetJsonStringField_ExportBpy(Root, TEXT("bp_type"), BP->IsA<UAnimBlueprint>() ? TEXT("AnimBlueprint") : TEXT("Normal"));

	TArray<FString> Lines;
	Lines.Add(TEXT("# -*- coding: utf-8 -*-"));
	Lines.Add(TEXT("\"\"\""));
	Lines.Add(TEXT("UE Blueprint Export"));
	Lines.Add(FString::Printf(TEXT("path:    %s"), *BlueprintAssetPath));
	Lines.Add(FString::Printf(TEXT("Parent:  %s"), *Parent));
	Lines.Add(FString::Printf(TEXT("Type:    %s"), *BlueprintType));
	Lines.Add(TEXT("Schema:  1"));
	Lines.Add(TEXT("\"\"\""));
	Lines.Add(TEXT("import json"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("# bp contains the full Blueprint structure in a single LLM-readable Python file."));
	Lines.Add(FString::Printf(TEXT("bp = json.loads(%s)"), *MakePythonMultilineStringLiteral_ExportBpy(PrettyJson)));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("def get_blueprint_data():"));
	Lines.Add(TEXT("    return json.loads(json.dumps(bp))"));
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("if __name__ == \"__main__\":"));
	Lines.Add(TEXT("    print(json.dumps(bp, ensure_ascii=False, indent=2))"));

	OutBpyText = FString::Join(Lines, TEXT("\n"));
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

		Out += FString::Printf(
			TEXT("bp.var(%s, %s, container=%s, default=%s)\n"),
			*MakePythonStringLiteral_ExportBpy(Var.VarName.ToString()),
			*MakePythonStringLiteral_ExportBpy(TypeStr),
			*MakePythonStringLiteral_ExportBpy(ContainerStr),
			*MakePythonStringLiteral_ExportBpy(DefaultValue));
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
			if (CastField<FArrayProperty>(Prop) || CastField<FSetProperty>(Prop) || CastField<FMapProperty>(Prop))
				continue;

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

		// Skip arrays/sets/maps — they're complex, importer doesn't handle them
		if (CastField<FArrayProperty>(Prop)
			|| CastField<FSetProperty>(Prop)
			|| CastField<FMapProperty>(Prop))
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
			FString ClassName = CompClass ? CompClass->GetName() : TEXT("Unknown");
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
	TArray<UK2Node*> AllNodes;

	for (UEdGraphNode* N : Graph->Nodes)
	{
		UK2Node* K2 = Cast<UK2Node>(N);
		if (!K2) continue;
		if (K2->IsA<UK2Node_Knot>()) continue;
		AllNodes.Add(K2);
	}

	AllNodes = TopologicalSort(AllNodes);
	for (UK2Node* Node : AllNodes)
	{
		NodeInfos.Add(BuildNodeInfo_ExportBpy(Node));
	}

	AssignReadableNames(NodeInfos);

	TMap<UK2Node*, FString> NodeVarMap;
	TMap<FString, FString> NodeGuidMap;
	TMap<FString, FVector2D> NodePosMap;
	TMap<FString, FString> PinAliasMap;
	TMap<FString, FString> PinIdMap;
	TMap<FString, TMap<FString, FString>> NodePropsMap;
	for (int32 i = 0; i < AllNodes.Num(); i++)
	{
		NodeVarMap.Add(AllNodes[i], NodeInfos[i].VarName);
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
		if (NodeInfos[i].NodeProps.Num() > 0)
		{
			NodePropsMap.Add(NodeInfos[i].VarName, NodeInfos[i].NodeProps);
		}
	}

	// ── Determine graph context header ─────────────────────────────────────────
	UBlueprint* OwnerBP = Cast<UBlueprint>(Graph->GetOuter());
	bool bIsFunction = OwnerBP && OwnerBP->FunctionGraphs.Contains(Graph);
	bool bIsMacro    = OwnerBP && OwnerBP->MacroGraphs.Contains(Graph);

	FString GraphName = Graph->GetName();
	OutModuleName = BuildModuleName_ExportBpy(Prefix, GraphName);
	const FString MetaModuleName = OutModuleName + TEXT("_meta");

	FString Lines;
	Lines += TEXT("# Auto-generated by ExportBpy\n");
	Lines += TEXT("import importlib.util\n");
	Lines += TEXT("import os\n");
	Lines += TEXT("from ue_bp_dsl import *\n");
	Lines += TEXT("\n");
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
		for (UK2Node* K2 : AllNodes)
		{
			if (auto* FE = Cast<UK2Node_FunctionEntry>(K2))
			{
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
		if (!InputsStr.IsEmpty())
			Args += FString::Printf(TEXT(", inputs=[%s]"), *InputsStr);
		if (!OutputsStr.IsEmpty())
			Args += FString::Printf(TEXT(", outputs=[%s]"), *OutputsStr);
		CtxHeader = FString::Printf(TEXT("with bp.function(%s) as g:"), *Args);
	}
	else if (bIsMacro)
	{
		CtxHeader = FString::Printf(TEXT("with bp.macro(%s) as g:"), *MakePythonStringLiteral_ExportBpy(GraphName));
	}
	else
	{
		CtxHeader = FString::Printf(TEXT("with bp.event_graph(%s) as g:"), *MakePythonStringLiteral_ExportBpy(GraphName));
	}

	Lines += TEXT("    ") + CtxHeader + TEXT("\n\n");

	auto AppendConnectionSections = [&](FString& InOutLines)
	{
		TArray<FString> DataLines;
		TArray<FString> ExecLines;
		for (int32 Index = 0; Index < AllNodes.Num(); ++Index)
		{
			UK2Node* SrcNode = AllNodes[Index];
			const FString& SrcVar = NodeInfos[Index].VarName;
			for (UEdGraphPin* SrcPin : SrcNode->Pins)
			{
				if (SrcPin->Direction != EGPD_Output)
				{
					continue;
				}

				for (UEdGraphPin* RawDst : SrcPin->LinkedTo)
				{
					UEdGraphPin* DstPin = ResolveRerouteChain(RawDst);
					if (!DstPin)
					{
						continue;
					}

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
					if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						ExecLines.Add(ConnLine);
					}
					else
					{
						DataLines.Add(ConnLine);
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
		Lines.Add(FString::Printf(TEXT("%s.pin(%s, %s)"),
			*Info.VarName,
			*MakePythonStringLiteral_ExportBpy(KV.Key),
			*FormatPythonValueLiteral_ExportBpy(KV.Value)));
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
	if (!Pin) return nullptr;
	const int32 MaxDepth = 64;
	int32 Depth = 0;
	while (Pin && Depth++ < MaxDepth)
	{
		UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Pin->GetOwningNode());
		if (!Knot) return Pin;
		// Follow through the knot
		UEdGraphPin* OutPin = Knot->GetOutputPin();
		if (!OutPin || OutPin->LinkedTo.IsEmpty()) return nullptr;
		Pin = OutPin->LinkedTo[0];
	}
	return Pin;
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
		VObj->SetBoolField(TEXT("replicated"), (Var.PropertyFlags & CPF_Net) != 0);
		VObj->SetStringField(TEXT("rep_notify"), Var.RepNotifyFunc.ToString());
		VObj->SetBoolField(TEXT("instance_editable"), (Var.PropertyFlags & CPF_Edit) != 0);
		VObj->SetStringField(TEXT("tooltip"),  Var.MetaDataArray.IsEmpty() ? TEXT("") :
			Var.MetaDataArray[0].DataValue);
		Vars.Add(MakeShared<FJsonValueObject>(VObj));
	}
	Root->SetArrayField(TEXT("variables"), Vars);

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
					if (CastField<FArrayProperty>(Prop) || CastField<FSetProperty>(Prop) || CastField<FMapProperty>(Prop)) continue;

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

	// Function graphs
	for (UEdGraph* G : BP->FunctionGraphs)
		if (G) Graphs.Add(MakeShared<FJsonValueObject>(SerializeGraph(G)));

	// Macro graphs
	for (UEdGraph* G : BP->MacroGraphs)
		if (G) Graphs.Add(MakeShared<FJsonValueObject>(SerializeGraph(G)));

	// Event graphs
	for (UEdGraph* G : BP->UbergraphPages)
		if (G) Graphs.Add(MakeShared<FJsonValueObject>(SerializeGraph(G)));

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

	// Determine graph type
	FString GType = TEXT("event_graph");
	if (Graph->GetOuter() && Graph->GetOuter()->IsA<UBlueprint>())
	{
		UBlueprint* BP = Cast<UBlueprint>(Graph->GetOuter());
		if (BP && BP->FunctionGraphs.Contains(Graph))
			GType = TEXT("function");
		else if (BP && BP->MacroGraphs.Contains(Graph))
			GType = TEXT("macro");
	}
	GObj->SetStringField(TEXT("graph_type"), GType);

	// Function inputs/outputs from FunctionEntry node
	TArray<TSharedPtr<FJsonValue>> Inputs, Outputs;
	GObj->SetArrayField(TEXT("inputs"),   Inputs);
	GObj->SetArrayField(TEXT("outputs"),  Outputs);
	GObj->SetBoolField(TEXT("is_pure"),   false);
	GObj->SetStringField(TEXT("category"), TEXT(""));

	// Nodes
	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		UK2Node* K2 = Cast<UK2Node>(N);
		if (!K2) continue;
		// Skip reroute knots
		if (K2->IsA<UK2Node_Knot>()) continue;
		Nodes.Add(MakeShared<FJsonValueObject>(SerializeNode(K2)));
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
	else if (auto* TL = Cast<UK2Node_Timeline>(Node))
		MemberName = TL->TimelineName.ToString();

	NObj->SetStringField(TEXT("member_name"),  MemberName);
	NObj->SetStringField(TEXT("function_ref"), FunctionRef);
	NObj->SetStringField(TEXT("target_type"),  TargetType);
	NObj->SetStringField(TEXT("tunnel_type"),  TEXT(""));
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
	for (UEdGraphPin* Pin : Node->Pins)
	{
		const FString PinDefaultValue = GetPinDefaultValue_ExportBpy(Pin);
		if (!PinDefaultValue.IsEmpty())
		{
			DefaultsObj->SetStringField(Pin->PinName.ToString(), PinDefaultValue);
		}
	}
	NObj->SetObjectField(TEXT("defaults"), DefaultsObj);

	auto NodePropsObj = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Entry : Info.NodeProps)
	{
		NodePropsObj->SetStringField(Entry.Key, Entry.Value);
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

// ─── SerializeConnections ─────────────────────────────────────────────────────

TArray<TSharedPtr<FJsonValue>> UBPDirectExporter::SerializeConnections(UEdGraph* Graph)
{
	TArray<TSharedPtr<FJsonValue>> Result;

	// Build uid map (skip knots)
	TMap<UEdGraphNode*, FString> NodeUidMap;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N->IsA<UK2Node_Knot>()) continue;
		NodeUidMap.Add(N, N->NodeGuid.ToString());
	}

	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N->IsA<UK2Node_Knot>()) continue;
		FString* SrcUid = NodeUidMap.Find(N);
		if (!SrcUid) continue;

		for (UEdGraphPin* SrcPin : N->Pins)
		{
			if (SrcPin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* RawDst : SrcPin->LinkedTo)
			{
				UEdGraphPin* DstPin = ResolveRerouteChain(RawDst);
				if (!DstPin) continue;
				FString* DstUid = NodeUidMap.Find(DstPin->GetOwningNode());
				if (!DstUid) continue;

				auto CObj = MakeShared<FJsonObject>();
				CObj->SetStringField(TEXT("src_node"), *SrcUid);
				CObj->SetStringField(TEXT("src_pin"),  SrcPin->PinName.ToString());
				CObj->SetStringField(TEXT("dst_node"), *DstUid);
				CObj->SetStringField(TEXT("dst_pin"),  DstPin->PinName.ToString());
				Result.Add(MakeShared<FJsonValueObject>(CObj));
			}
		}
	}

	return Result;
}
