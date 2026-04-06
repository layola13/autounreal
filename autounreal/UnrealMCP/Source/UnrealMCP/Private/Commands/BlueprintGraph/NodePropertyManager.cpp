#include "Commands/BlueprintGraph/NodePropertyManager.h"
#include "Commands/BlueprintGraph/Nodes/SwitchEnumEditor.h"
#include "Commands/BlueprintGraph/Nodes/ExecutionSequenceEditor.h"
#include "Commands/BlueprintGraph/Nodes/MakeArrayEditor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Switch.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MakeArray.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_Select.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_CastByteToEnum.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "EditorAssetLibrary.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Json.h"

static FString NormalizeFunctionName_NPM(const FString& InName)
{
	FString OutName = InName;
	OutName.ReplaceInline(TEXT(" "), TEXT(""));
	return OutName;
}

static FString NormalizeLoosePinName_NPM(const FString& InName)
{
	FString Name = InName;
	TArray<FString> Parts;
	Name.ParseIntoArray(Parts, TEXT("_"), true);

	auto IsHexLike = [](const FString& Value) -> bool
	{
		if (Value.Len() < 8)
		{
			return false;
		}

		for (const TCHAR Char : Value)
		{
			if (!FChar::IsHexDigit(Char))
			{
				return false;
			}
		}

		return true;
	};

	if (Parts.Num() >= 3 && Parts[Parts.Num() - 2].IsNumeric() && IsHexLike(Parts.Last()))
	{
		Parts.SetNum(Parts.Num() - 2);
		Name = FString::Join(Parts, TEXT("_"));
	}

	return Name;
}

static bool DoesPinNameMatchLoose_NPM(const UEdGraphPin* Pin, const FString& DesiredName)
{
	if (!Pin || DesiredName.IsEmpty())
	{
		return false;
	}

	const FString PinName = Pin->PinName.ToString();
	if (PinName.Equals(DesiredName, ESearchCase::IgnoreCase))
	{
		return true;
	}

	if (NormalizeLoosePinName_NPM(PinName).Equals(NormalizeLoosePinName_NPM(DesiredName), ESearchCase::IgnoreCase))
	{
		return true;
	}

	if (!Pin->PinFriendlyName.IsEmptyOrWhitespace() &&
		Pin->PinFriendlyName.ToString().Equals(DesiredName, ESearchCase::IgnoreCase))
	{
		return true;
	}

	return false;
}

static UEdGraphPin* FindPinRecursiveByLooseName_NPM(
	const TArray<UEdGraphPin*>& Pins,
	const FString& DesiredName,
	const EEdGraphPinDirection Direction)
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin)
		{
			continue;
		}

		if (Pin->Direction == Direction && DoesPinNameMatchLoose_NPM(Pin, DesiredName))
		{
			return Pin;
		}

		if (Pin->SubPins.Num() > 0)
		{
			if (UEdGraphPin* SubPin = FindPinRecursiveByLooseName_NPM(Pin->SubPins, DesiredName, Direction))
			{
				return SubPin;
			}
		}
	}

	return nullptr;
}

static UEdGraphPin* FindPinByLooseName_NPM(
	UEdGraphNode* Node,
	const FString& DesiredName,
	const EEdGraphPinDirection Direction)
{
	return Node ? FindPinRecursiveByLooseName_NPM(Node->Pins, DesiredName, Direction) : nullptr;
}

static UClass* ResolveClassFromName_NPM(const FString& ClassName)
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	if (ClassName.StartsWith(TEXT("/")))
	{
		return Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *ClassName));
	}

	if (ClassName.Contains(TEXT(".")))
	{
		const FString Path = FString::Printf(TEXT("/Script/%s"), *ClassName);
		if (UClass* Loaded = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *Path)))
		{
			return Loaded;
		}
	}

	if (UClass* Found = FindFirstObject<UClass>(*ClassName))
	{
		return Found;
	}

	if (!ClassName.StartsWith(TEXT("A")) && !ClassName.StartsWith(TEXT("U")))
	{
		if (UClass* FoundA = FindFirstObject<UClass>(*(TEXT("A") + ClassName)))
		{
			return FoundA;
		}
		if (UClass* FoundU = FindFirstObject<UClass>(*(TEXT("U") + ClassName)))
		{
			return FoundU;
		}
	}

	const FString Path = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
	if (UClass* Loaded = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *Path)))
	{
		return Loaded;
	}

	return nullptr;
}

static bool UpdateCastTargetType_NPM(UK2Node* Node, UClass* TargetClass, UEdGraph* Graph)
{
	if (!Node || !TargetClass)
	{
		return false;
	}

	if (UK2Node_DynamicCast* DynamicCastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		DynamicCastNode->Modify();
		DynamicCastNode->TargetType = TargetClass;
		DynamicCastNode->ReconstructNode();
		if (Graph)
		{
			Graph->NotifyGraphChanged();
		}
		return true;
	}

	if (UK2Node_ClassDynamicCast* ClassDynamicCastNode = Cast<UK2Node_ClassDynamicCast>(Node))
	{
		ClassDynamicCastNode->Modify();
		ClassDynamicCastNode->TargetType = TargetClass;
		ClassDynamicCastNode->ReconstructNode();
		if (Graph)
		{
			Graph->NotifyGraphChanged();
		}
		return true;
	}

	return false;
}

static UFunction* FindFunctionInClass_NPM(UClass* InClass, const FString& FunctionName)
{
	if (!InClass)
	{
		return nullptr;
	}

	const FName FuncName(*FunctionName);
	if (UFunction* Found = InClass->FindFunctionByName(FuncName))
	{
		return Found;
	}

	const FString Normalized = NormalizeFunctionName_NPM(FunctionName);
	if (!Normalized.Equals(FunctionName, ESearchCase::CaseSensitive))
	{
		if (UFunction* Found = InClass->FindFunctionByName(FName(*Normalized)))
		{
			return Found;
		}
	}

	for (UClass* Super = InClass->GetSuperClass(); Super; Super = Super->GetSuperClass())
	{
		if (UFunction* Found = Super->FindFunctionByName(FuncName))
		{
			return Found;
		}
		if (!Normalized.Equals(FunctionName, ESearchCase::CaseSensitive))
		{
			if (UFunction* Found = Super->FindFunctionByName(FName(*Normalized)))
			{
				return Found;
			}
		}
	}

	return nullptr;
}

static bool TryGetBoolField_NPM(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, bool& OutValue)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	return JsonObject->TryGetBoolField(FieldName, OutValue);
}

static bool TryGetJsonFieldAsString_NPM(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& OutValue)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonValue> FieldValue = JsonObject->Values.FindRef(FieldName);
	if (!FieldValue.IsValid())
	{
		return false;
	}

	switch (FieldValue->Type)
	{
	case EJson::String:
		OutValue = FieldValue->AsString();
		return true;

	case EJson::Number:
		OutValue = FString::SanitizeFloat(FieldValue->AsNumber());
		return true;

	case EJson::Boolean:
		OutValue = FieldValue->AsBool() ? TEXT("true") : TEXT("false");
		return true;

	default:
		return false;
	}
}

static bool TryGetNonEmptyStringField_NPM(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& OutValue)
{
	if (!TryGetJsonFieldAsString_NPM(JsonObject, FieldName, OutValue))
	{
		return false;
	}

	OutValue.TrimStartAndEndInline();
	return !OutValue.IsEmpty();
}

static bool ResolveLegacyDefaultValue_NPM(const TSharedPtr<FJsonObject>& Params, FString& OutDefaultValue)
{
	if (TryGetJsonFieldAsString_NPM(Params, TEXT("default_value"), OutDefaultValue))
	{
		return true;
	}

	if (TryGetJsonFieldAsString_NPM(Params, TEXT("property_value"), OutDefaultValue))
	{
		return true;
	}

	FString PropNameFallback;
	if (TryGetNonEmptyStringField_NPM(Params, TEXT("property_name"), PropNameFallback))
	{
		OutDefaultValue = PropNameFallback;
		return true;
	}

	FString AltValue;
	if (TryGetNonEmptyStringField_NPM(Params, TEXT("target_function"), AltValue) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("event_type"), AltValue) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("enum_type"), AltValue) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("new_type"), AltValue) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("pin_type"), AltValue) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("target_class"), AltValue) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("target_type"), AltValue))
	{
		OutDefaultValue = AltValue;
		return true;
	}

	return false;
}

static UObject* TryLoadDefaultObject_NPM(const FString& ObjectPath)
{
	if (ObjectPath.IsEmpty())
	{
		return nullptr;
	}

	if (UObject* FoundObject = FindObject<UObject>(nullptr, *ObjectPath))
	{
		return FoundObject;
	}

	if (UObject* LoadedObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		return LoadedObject;
	}

	if (UClass* LoadedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ObjectPath))
	{
		return LoadedClass;
	}

	return UEditorAssetLibrary::LoadAsset(ObjectPath);
}

static UObject* ResolveDefaultObjectForPin_NPM(const UEdGraphPin* Pin, const FString& ObjectPath, FString& OutErrorMessage)
{
	if (!Pin)
	{
		OutErrorMessage = TEXT("Invalid pin");
		return nullptr;
	}

	UObject* LoadedObject = TryLoadDefaultObject_NPM(ObjectPath);
	if (!LoadedObject)
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to load object from path: %s"), *ObjectPath);
		return nullptr;
	}

	const bool bClassLikePin =
		Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;

	if (bClassLikePin)
	{
		if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(LoadedObject))
		{
			if (BlueprintAsset->GeneratedClass)
			{
				return BlueprintAsset->GeneratedClass;
			}

			if (BlueprintAsset->SkeletonGeneratedClass)
			{
				return BlueprintAsset->SkeletonGeneratedClass;
			}
		}
	}

	return LoadedObject;
}

static void FinalizePinDefaultChange_NPM(UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return;
	}

	UEdGraphNode* Node = Pin->GetOwningNode();
	if (!Node)
	{
		return;
	}

	Node->PinDefaultValueChanged(Pin);

	if (Pin->bOrphanedPin && Pin->DoesDefaultValueMatchAutogenerated())
	{
		Node->PinConnectionListChanged(Pin);
	}
}

static void NotifyPinDefaultGraphChanged_NPM(UBlueprint* Blueprint, UEdGraph* Graph, UK2Node* Node)
{
	if (Graph)
	{
		Graph->NotifyGraphChanged();
	}

	if (!Blueprint)
	{
		if (Node)
		{
			Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
		}
		else if (Graph)
		{
			Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
		}
	}

	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

static bool ApplyPinDefaultSpecInternal_NPM(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	UK2Node* Node,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutAppliedResult,
	FString& OutErrorMessage,
	const bool bNotifyGraph)
{
	if (!Node || !Params.IsValid())
	{
		OutErrorMessage = TEXT("Invalid pin default parameters");
		return false;
	}

	FString PinName;
	if (!TryGetNonEmptyStringField_NPM(Params, TEXT("pin_name"), PinName))
	{
		OutErrorMessage = TEXT("Missing 'pin_name' parameter");
		return false;
	}

	UEdGraphPin* Pin = FindPinByLooseName_NPM(Node, PinName, EGPD_Input);
	if (!Pin)
	{
		OutErrorMessage = FString::Printf(TEXT("Pin not found: %s"), *PinName);
		return false;
	}

	if (Pin->Direction != EGPD_Input)
	{
		OutErrorMessage = FString::Printf(TEXT("Pin does not accept defaults because it is not an input pin: %s"), *PinName);
		return false;
	}

	if (Pin->LinkedTo.Num() > 0)
	{
		OutErrorMessage = FString::Printf(TEXT("Pin already has connections and cannot accept a default value: %s"), *PinName);
		return false;
	}

	if (Pin->bDefaultValueIsIgnored)
	{
		OutErrorMessage = FString::Printf(TEXT("Pin ignores default values: %s"), *PinName);
		return false;
	}

	if (Pin->bDefaultValueIsReadOnly)
	{
		OutErrorMessage = FString::Printf(TEXT("Pin default value is read-only: %s"), *PinName);
		return false;
	}

	const UEdGraphSchema* Schema = Pin->GetSchema();
	if (!Schema)
	{
		OutErrorMessage = TEXT("Pin schema is not available");
		return false;
	}

	bool bClearDefault = false;
	TryGetBoolField_NPM(Params, TEXT("clear_default"), bClearDefault);

	FString ValueKind;
	TryGetNonEmptyStringField_NPM(Params, TEXT("value_kind"), ValueKind);
	ValueKind.ToLowerInline();

	FString DefaultValue;
	FString DefaultObjectPath;
	FString DefaultTextString;

	const bool bHasDefaultObjectPath =
		TryGetNonEmptyStringField_NPM(Params, TEXT("default_object_path"), DefaultObjectPath) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("default_object"), DefaultObjectPath);

	const bool bHasDefaultText =
		TryGetNonEmptyStringField_NPM(Params, TEXT("default_text"), DefaultTextString) ||
		TryGetNonEmptyStringField_NPM(Params, TEXT("default_text_value"), DefaultTextString);

	const bool bHasDefaultValue = ResolveLegacyDefaultValue_NPM(Params, DefaultValue);

	Node->Modify();
	Pin->Modify();

	if (bClearDefault)
	{
		Schema->ResetPinToAutogeneratedDefaultValue(Pin, false);
		FinalizePinDefaultChange_NPM(Pin);
	}
	else
	{
		const bool bTreatAsText =
			ValueKind == TEXT("text") ||
			(ValueKind.IsEmpty() && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text && (bHasDefaultText || bHasDefaultValue));

		const bool bTreatAsObject =
			ValueKind == TEXT("object") ||
			ValueKind == TEXT("class") ||
			(ValueKind.IsEmpty() && bHasDefaultObjectPath &&
				(Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
				 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
				 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface ||
				 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
				 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass));

		if (bTreatAsText)
		{
			const FString TextSource = bHasDefaultText ? DefaultTextString : DefaultValue;
			const FText DefaultText = FText::FromString(TextSource);
			const FString ValidationError = Schema->IsPinDefaultValid(Pin, FString(), nullptr, DefaultText);
			if (!ValidationError.IsEmpty())
			{
				OutErrorMessage = FString::Printf(TEXT("Invalid text default for pin '%s': %s"), *PinName, *ValidationError);
				return false;
			}

			Schema->TrySetDefaultText(*Pin, DefaultText, false);
		}
		else if (bTreatAsObject)
		{
			FString ObjectPathToUse = DefaultObjectPath;
			if (ObjectPathToUse.IsEmpty() && bHasDefaultValue)
			{
				ObjectPathToUse = DefaultValue;
			}

			if (ObjectPathToUse.IsEmpty())
			{
				OutErrorMessage = FString::Printf(TEXT("Missing object path for pin '%s'"), *PinName);
				return false;
			}

			const bool bSoftReferencePin =
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;

			if (bSoftReferencePin)
			{
				const FString ValidationError = Schema->IsPinDefaultValid(Pin, ObjectPathToUse, nullptr, FText::GetEmpty());
				if (!ValidationError.IsEmpty())
				{
					OutErrorMessage = FString::Printf(TEXT("Invalid soft object default for pin '%s': %s"), *PinName, *ValidationError);
					return false;
				}

				Schema->TrySetDefaultValue(*Pin, ObjectPathToUse, false);
			}
			else
			{
				FString ResolveError;
				UObject* DefaultObject = ResolveDefaultObjectForPin_NPM(Pin, ObjectPathToUse, ResolveError);
				if (!DefaultObject)
				{
					OutErrorMessage = FString::Printf(TEXT("Failed to resolve object default for pin '%s': %s"), *PinName, *ResolveError);
					return false;
				}

				const FString ValidationError = Schema->IsPinDefaultValid(Pin, FString(), DefaultObject, FText::GetEmpty());
				if (!ValidationError.IsEmpty())
				{
					OutErrorMessage = FString::Printf(TEXT("Invalid object default for pin '%s': %s"), *PinName, *ValidationError);
					return false;
				}

				Schema->TrySetDefaultObject(*Pin, DefaultObject, false);
			}
		}
		else
		{
			if (!bHasDefaultValue)
			{
				OutErrorMessage = TEXT("Missing 'default_value' parameter");
				return false;
			}

			const FString ValidationError = Schema->IsPinDefaultValid(Pin, DefaultValue, nullptr, FText::GetEmpty());
			if (!ValidationError.IsEmpty())
			{
				OutErrorMessage = FString::Printf(TEXT("Invalid default value for pin '%s': %s"), *PinName, *ValidationError);
				return false;
			}

			Schema->TrySetDefaultValue(*Pin, DefaultValue, false);
		}

		FinalizePinDefaultChange_NPM(Pin);
	}

	if (bNotifyGraph)
	{
		NotifyPinDefaultGraphChanged_NPM(Blueprint, Graph, Node);
	}

	OutAppliedResult = MakeShareable(new FJsonObject);
	OutAppliedResult->SetBoolField(TEXT("success"), true);
	OutAppliedResult->SetStringField(TEXT("pin_name"), PinName);
	OutAppliedResult->SetStringField(TEXT("pin_category"), Pin->PinType.PinCategory.ToString());
	OutAppliedResult->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	OutAppliedResult->SetStringField(TEXT("default_object"), Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : FString());
	OutAppliedResult->SetStringField(TEXT("default_text_value"), Pin->DefaultTextValue.ToString());
	OutAppliedResult->SetStringField(TEXT("default_as_string"), Pin->GetDefaultAsString());
	OutAppliedResult->SetBoolField(TEXT("cleared_to_autogenerated"), bClearDefault);
	OutAppliedResult->SetStringField(TEXT("value_kind"), ValueKind.IsEmpty() ? TEXT("auto") : ValueKind);
	return true;
}

bool FNodePropertyManager::ExtractPinDefaultSpecs(const TSharedPtr<FJsonObject>& Params, TArray<TSharedPtr<FJsonValue>>& OutPinDefaults)
{
	OutPinDefaults.Reset();
	if (!Params.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* PinDefaultsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("pin_defaults"), PinDefaultsArray))
	{
		OutPinDefaults = *PinDefaultsArray;
		return true;
	}

	const TSharedPtr<FJsonObject>* PinDefaultsObject = nullptr;
	if (Params->TryGetObjectField(TEXT("pin_defaults"), PinDefaultsObject) ||
		Params->TryGetObjectField(TEXT("pin_default"), PinDefaultsObject))
	{
		OutPinDefaults.Add(MakeShared<FJsonValueObject>(*PinDefaultsObject));
		return true;
	}

	return false;
}

bool FNodePropertyManager::ApplyPinDefaultSpec(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	UK2Node* Node,
	const TSharedPtr<FJsonObject>& Params,
	TSharedPtr<FJsonObject>& OutAppliedResult,
	FString& OutErrorMessage)
{
	return ApplyPinDefaultSpecInternal_NPM(Blueprint, Graph, Node, Params, OutAppliedResult, OutErrorMessage, true);
}

bool FNodePropertyManager::ApplyPinDefaults(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	UK2Node* Node,
	const TArray<TSharedPtr<FJsonValue>>& PinDefaults,
	TArray<TSharedPtr<FJsonValue>>& OutAppliedResults,
	FString& OutErrorMessage)
{
	OutAppliedResults.Reset();
	if (!Node)
	{
		OutErrorMessage = TEXT("Invalid node for pin default application");
		return false;
	}

	for (int32 Index = 0; Index < PinDefaults.Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& PinDefaultValue = PinDefaults[Index];
		if (!PinDefaultValue.IsValid() || PinDefaultValue->Type != EJson::Object)
		{
			OutErrorMessage = FString::Printf(TEXT("pin_defaults[%d] must be an object"), Index);
			return false;
		}

		TSharedPtr<FJsonObject> AppliedResult;
		FString ApplyError;
		if (!ApplyPinDefaultSpecInternal_NPM(Blueprint, Graph, Node, PinDefaultValue->AsObject(), AppliedResult, ApplyError, false))
		{
			OutErrorMessage = FString::Printf(TEXT("pin_defaults[%d]: %s"), Index, *ApplyError);
			return false;
		}

		OutAppliedResults.Add(MakeShared<FJsonValueObject>(AppliedResult));
	}

	NotifyPinDefaultGraphChanged_NPM(Blueprint, Graph, Node);
	return true;
}

TSharedPtr<FJsonObject> FNodePropertyManager::SetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	// Validate parameters
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString NodeID;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
	{
		return CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	// ===================================================
	// CHECK FOR SEMANTIC ACTION (new mode)
	// ===================================================
	FString Action;
	bool bHasAction = Params->HasField(TEXT("action"));

	if (bHasAction)
	{
		if (Params->TryGetStringField(TEXT("action"), Action))
		{
			if (!Action.IsEmpty())
			{
				// Semantic editing mode - delegate to EditNode
				return EditNode(Params);
			}
		}
	}

	// ===================================================
	// LEGACY MODE: Simple property modification
	// ===================================================
	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		UE_LOG(LogTemp, Error, TEXT("SetNodeProperty: Missing 'property_name' parameter"));
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}

	if (!Params->HasField(TEXT("property_value")))
	{
		return CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
	}

	TSharedPtr<FJsonValue> PropertyValue = Params->Values.FindRef(TEXT("property_value"));

	// Get optional function name
	FString FunctionName;
	Params->TryGetStringField(TEXT("function_name"), FunctionName);

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Get the appropriate graph
	UEdGraph* Graph = GetGraph(Blueprint, FunctionName);
	if (!Graph)
	{
		if (FunctionName.IsEmpty())
		{
			return CreateErrorResponse(TEXT("Blueprint has no event graph"));
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
		}
	}

	// Find the node
	UEdGraphNode* Node = FindNodeByID(Graph, NodeID);
	if (!Node)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeID));
	}

	// Attempt to set property based on node type
	bool Success = false;

	// Try as Print node (UK2Node_CallFunction)
	UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node);
	if (CallFuncNode)
	{
		Success = SetPrintNodeProperty(CallFuncNode, PropertyName, PropertyValue);
	}

	// Try as Variable node
	if (!Success)
	{
		UK2Node* K2Node = Cast<UK2Node>(Node);
		if (K2Node)
		{
			Success = SetVariableNodeProperty(K2Node, PropertyName, PropertyValue);
		}
	}

	// Try generic properties
	if (!Success)
	{
		Success = SetGenericNodeProperty(Node, PropertyName, PropertyValue);
	}

	if (!Success)
	{
		return CreateErrorResponse(FString::Printf(
			TEXT("Failed to set property '%s' on node (property not supported or invalid value)"),
			*PropertyName));
	}

	// Notify changes
	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogTemp, Display,
		TEXT("Successfully set '%s' on node '%s' in %s"),
		*PropertyName, *NodeID, *BlueprintName);

	return CreateSuccessResponse(PropertyName);
}

TSharedPtr<FJsonObject> FNodePropertyManager::EditNode(const TSharedPtr<FJsonObject>& Params)
{
	// Validate parameters
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString NodeID;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
	{
		return CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	FString Action;
	if (!Params->TryGetStringField(TEXT("action"), Action))
	{
		return CreateErrorResponse(TEXT("Missing 'action' parameter"));
	}

	// Get optional function name
	FString FunctionName;
	Params->TryGetStringField(TEXT("function_name"), FunctionName);

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Get the appropriate graph
	UEdGraph* Graph = GetGraph(Blueprint, FunctionName);
	if (!Graph)
	{
		if (FunctionName.IsEmpty())
		{
			return CreateErrorResponse(TEXT("Blueprint has no event graph"));
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
		}
	}

	// Find the node
	UEdGraphNode* Node = FindNodeByID(Graph, NodeID);
	if (!Node)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeID));
	}

	// Cast to K2Node (edit operations require K2Node)
	UK2Node* K2Node = Cast<UK2Node>(Node);
	if (!K2Node)
	{
		return CreateErrorResponse(TEXT("Node is not a K2Node (cannot edit this node type)"));
	}

	// Dispatch the edit action
	TSharedPtr<FJsonObject> ActionResponse = DispatchEditAction(K2Node, Graph, Action, Params);
	if (!ActionResponse.IsValid())
	{
		return CreateErrorResponse(TEXT("Failed to dispatch edit action"));
	}

	const bool bSuccess = !ActionResponse->HasField(TEXT("success")) || ActionResponse->GetBoolField(TEXT("success"));
	if (!bSuccess)
	{
		return ActionResponse;
	}

	if (Action.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase))
	{
		return ActionResponse;
	}

	TArray<TSharedPtr<FJsonValue>> PinDefaults;
	if (ExtractPinDefaultSpecs(Params, PinDefaults) && PinDefaults.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> AppliedDefaults;
		FString PinDefaultError;
		if (!ApplyPinDefaults(Blueprint, Graph, K2Node, PinDefaults, AppliedDefaults, PinDefaultError))
		{
			Graph->NotifyGraphChanged();
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			return CreateErrorResponse(FString::Printf(TEXT("Edit action succeeded but applying pin defaults failed: %s"), *PinDefaultError));
		}

		ActionResponse->SetArrayField(TEXT("pin_defaults_applied"), AppliedDefaults);
		ActionResponse->SetNumberField(TEXT("pin_defaults_applied_count"), AppliedDefaults.Num());
		return ActionResponse;
	}

	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return ActionResponse;
}

TSharedPtr<FJsonObject> FNodePropertyManager::DispatchEditAction(
	UK2Node* Node,
	UEdGraph* Graph,
	const FString& Action,
	const TSharedPtr<FJsonObject>& Params)
{
	if (!Node || !Graph || !Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid node or graph"));
	}

	// === SWITCHENUM: Set enum type and auto-generate pins ===
	if (Action.Equals(TEXT("set_enum_type"), ESearchCase::IgnoreCase))
	{
		FString EnumPath;
		if (!Params->TryGetStringField(TEXT("enum_type"), EnumPath))
		{
			if (!Params->TryGetStringField(TEXT("enum_path"), EnumPath))
			{
				return CreateErrorResponse(TEXT("Missing 'enum_type' or 'enum_path' parameter"));
			}
		}

		bool bSuccess = FSwitchEnumEditor::SetEnumType(Node, Graph, EnumPath);

		if (!bSuccess)
		{
			UEnum* LoadedEnum = LoadObject<UEnum>(nullptr, *EnumPath);
			if (!LoadedEnum)
			{
				LoadedEnum = FindObject<UEnum>(nullptr, *EnumPath);
			}

			if (LoadedEnum)
			{
				if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
				{
					SelectNode->Modify();
					SelectNode->SetEnum(LoadedEnum, true);
					SelectNode->ReconstructNode();
					Graph->NotifyGraphChanged();
					bSuccess = true;
				}
				else if (UK2Node_CastByteToEnum* CastByteNode = Cast<UK2Node_CastByteToEnum>(Node))
				{
					CastByteNode->Modify();
					CastByteNode->Enum = LoadedEnum;
					CastByteNode->ReconstructNode();
					Graph->NotifyGraphChanged();
					bSuccess = true;
				}
			}
		}

		if (bSuccess)
		{
			TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("action"), TEXT("set_enum_type"));
			Response->SetStringField(TEXT("enum_type"), EnumPath);
			return Response;
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to set enum type: %s"), *EnumPath));
		}
	}

	// === EXECUTIONSEQUENCE/MAKEARRAY: Add pin ===
	if (Action.Equals(TEXT("add_pin"), ESearchCase::IgnoreCase))
	{
		bool bSuccess = FExecutionSequenceEditor::AddExecutionPin(Node, Graph);

		// If ExecutionSequence failed, try MakeArray
		if (!bSuccess)
		{
			bSuccess = FMakeArrayEditor::AddArrayElementPin(Node, Graph);
		}

		if (bSuccess)
		{
			TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("action"), TEXT("add_pin"));
			return Response;
		}
		else
		{
			return CreateErrorResponse(TEXT("Failed to add pin"));
		}
	}

	// === EXECUTIONSEQUENCE/MAKEARRAY: Remove pin ===
	if (Action.Equals(TEXT("remove_pin"), ESearchCase::IgnoreCase))
	{
		FString PinName;
		if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
		{
			return CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
		}

		bool bSuccess = FExecutionSequenceEditor::RemoveExecutionPin(Node, Graph, PinName);
		if (!bSuccess)
		{
			// Try MakeArray if ExecutionSequence failed
			bSuccess = FMakeArrayEditor::RemoveArrayElementPin(Node, Graph, PinName);
		}

		if (!bSuccess)
		{
			// Generic fallback: remove an explicitly named pin from any node type.
			// This is mainly used to clean stale/orphaned pins after reference retargeting.
			UEdGraphPin* Pin = FindPinByLooseName_NPM(Node, PinName, EGPD_Input);
			if (!Pin)
			{
				Pin = FindPinByLooseName_NPM(Node, PinName, EGPD_Output);
			}
			if (Pin)
			{
				Node->Modify();
				Pin->Modify();
				Pin->BreakAllPinLinks();
				Node->RemovePin(Pin);
				Graph->NotifyGraphChanged();
				bSuccess = true;
			}
		}

		if (bSuccess)
		{
			TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("action"), TEXT("remove_pin"));
			Response->SetStringField(TEXT("pin_name"), PinName);
			return Response;
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to remove pin: %s"), *PinName));
		}
	}

	// === MAKEARRAY: Set number of array elements ===
	if (Action.Equals(TEXT("set_num_elements"), ESearchCase::IgnoreCase))
	{
		double NumElementsDouble = 0.0;
		if (!Params->TryGetNumberField(TEXT("num_elements"), NumElementsDouble))
		{
			return CreateErrorResponse(TEXT("Missing 'num_elements' parameter"));
		}
		int32 NumElements = static_cast<int32>(NumElementsDouble);

		bool bSuccess = FMakeArrayEditor::SetNumArrayElements(Node, Graph, NumElements);
		if (bSuccess)
		{
			TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("action"), TEXT("set_num_elements"));
			Response->SetNumberField(TEXT("num_elements"), NumElements);
			return Response;
		}
		else
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to set array elements to %d"), NumElements));
		}
	}

	// === ANY K2 NODE: Split a struct pin ===
	if (Action.Equals(TEXT("split_pin"), ESearchCase::IgnoreCase))
	{
		FString PinName;
		if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
		{
			return CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
		}

		UEdGraphPin* Pin = FindPinByLooseName_NPM(Node, PinName, EGPD_Input);
		if (!Pin)
		{
			Pin = FindPinByLooseName_NPM(Node, PinName, EGPD_Output);
		}
		if (!Pin)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Pin not found: %s"), *PinName));
		}

		const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
		if (!Schema)
		{
			return CreateErrorResponse(TEXT("K2 graph schema not found"));
		}

		if (!Schema->CanSplitStructPin(*Pin))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Pin cannot be split: %s"), *PinName));
		}

		Node->Modify();
		Pin->Modify();
		Schema->SplitPin(Pin, false);

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("action"), TEXT("split_pin"));
		Response->SetStringField(TEXT("pin_name"), PinName);
		return Response;
	}

	// === ANY K2 NODE: Break links on a specific pin ===
	if (Action.Equals(TEXT("break_pin_links"), ESearchCase::IgnoreCase) ||
		Action.Equals(TEXT("disconnect_pin"), ESearchCase::IgnoreCase))
	{
		FString PinName;
		if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
		{
			return CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
		}

		UEdGraphPin* Pin = FindPinByLooseName_NPM(Node, PinName, EGPD_Input);
		if (!Pin)
		{
			Pin = FindPinByLooseName_NPM(Node, PinName, EGPD_Output);
		}
		if (!Pin)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Pin not found: %s"), *PinName));
		}

		const int32 BrokenLinkCount = Pin->LinkedTo.Num();
		Node->Modify();
		Pin->Modify();

		if (const UEdGraphSchema* Schema = Graph->GetSchema())
		{
			Schema->BreakPinLinks(*Pin, true);
		}
		else
		{
			Pin->BreakAllPinLinks(true);
		}

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("action"), Action);
		Response->SetStringField(TEXT("pin_name"), PinName);
		Response->SetNumberField(TEXT("broken_link_count"), BrokenLinkCount);
		return Response;
	}

	// === DYNAMICCAST / CLASSDYNAMICCAST: Set cast target class ===
	if (Action.Equals(TEXT("set_cast_target"), ESearchCase::IgnoreCase))
	{
		FString ClassName;
		if (!Params->TryGetStringField(TEXT("target_type"), ClassName))
		{
			if (!Params->TryGetStringField(TEXT("target_class"), ClassName))
			{
				return CreateErrorResponse(TEXT("Missing 'target_type' or 'target_class' parameter"));
			}
		}

		UClass* TargetClass = ResolveClassFromName_NPM(ClassName);
		if (!TargetClass)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to resolve cast target class: %s"), *ClassName));
		}

		if (!UpdateCastTargetType_NPM(Node, TargetClass, Graph))
		{
			return CreateErrorResponse(TEXT("Node is not a DynamicCast or ClassDynamicCast node"));
		}

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("action"), TEXT("set_cast_target"));
		Response->SetStringField(TEXT("target_type"), TargetClass->GetPathName());
		return Response;
	}

	// === CALLFUNCTION: Set function call ===
	if (Action.Equals(TEXT("set_function_call"), ESearchCase::IgnoreCase))
	{
		FString TargetFunction;
		if (!Params->TryGetStringField(TEXT("target_function"), TargetFunction))
		{
			return CreateErrorResponse(TEXT("Missing 'target_function' parameter"));
		}

		FString ClassName;
		if (!Params->TryGetStringField(TEXT("target_class"), ClassName))
		{
			Params->TryGetStringField(TEXT("target_blueprint"), ClassName);
		}

		UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
		if (!CallNode)
		{
			return CreateErrorResponse(TEXT("Node is not a CallFunction node"));
		}

		UFunction* TargetFunc = nullptr;
		if (!ClassName.IsEmpty())
		{
			UClass* TargetClass = ResolveClassFromName_NPM(ClassName);
			TargetFunc = FindFunctionInClass_NPM(TargetClass, TargetFunction);
		}
		else
		{
			if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
			{
				UClass* BPClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(BP);
				TargetFunc = FindFunctionInClass_NPM(BPClass, TargetFunction);
			}

			if (!TargetFunc)
			{
				TargetFunc = FindFunctionInClass_NPM(UKismetSystemLibrary::StaticClass(), TargetFunction);
			}
			if (!TargetFunc)
			{
				TargetFunc = FindFunctionInClass_NPM(UKismetMathLibrary::StaticClass(), TargetFunction);
			}
			if (!TargetFunc)
			{
				TargetFunc = FindFunctionInClass_NPM(UKismetStringLibrary::StaticClass(), TargetFunction);
			}
			if (!TargetFunc)
			{
				TargetFunc = FindFunctionInClass_NPM(UKismetArrayLibrary::StaticClass(), TargetFunction);
			}
			if (!TargetFunc)
			{
				TargetFunc = FindFunctionInClass_NPM(UGameplayStatics::StaticClass(), TargetFunction);
			}
		}

		if (!TargetFunc)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to resolve function: %s"), *TargetFunction));
		}

		CallNode->Modify();
		CallNode->SetFromFunction(TargetFunc);
		CallNode->ReconstructNode();

		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetBoolField(TEXT("success"), true);
		Response->SetStringField(TEXT("action"), TEXT("set_function_call"));
		Response->SetStringField(TEXT("target_function"), TargetFunction);
		return Response;
	}

	// === GENERIC: Set pin default value ===
	if (Action.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase))
	{
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
		TSharedPtr<FJsonObject> AppliedResult;
		FString ApplyError;
		if (!ApplyPinDefaultSpec(Blueprint, Graph, Node, Params, AppliedResult, ApplyError))
		{
			return CreateErrorResponse(ApplyError);
		}

		AppliedResult->SetStringField(TEXT("action"), TEXT("set_pin_default"));
		return AppliedResult;
	}

	// Unknown action
	return CreateErrorResponse(FString::Printf(TEXT("Unknown action: %s"), *Action));
}

bool FNodePropertyManager::SetPrintNodeProperty(
	UK2Node_CallFunction* PrintNode,
	const FString& PropertyName,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!PrintNode || !Value.IsValid())
	{
		return false;
	}

	// Handle "message" property
	if (PropertyName.Equals(TEXT("message"), ESearchCase::IgnoreCase))
	{
		FString MessageValue;
		if (Value->TryGetString(MessageValue))
		{
			UEdGraphPin* InStringPin = PrintNode->FindPin(TEXT("InString"));
			if (InStringPin)
			{
				InStringPin->DefaultValue = MessageValue;
				return true;
			}
		}
	}

	// Handle "duration" property
	if (PropertyName.Equals(TEXT("duration"), ESearchCase::IgnoreCase))
	{
		double DurationValue;
		if (Value->TryGetNumber(DurationValue))
		{
			UEdGraphPin* DurationPin = PrintNode->FindPin(TEXT("Duration"));
			if (DurationPin)
			{
				DurationPin->DefaultValue = FString::SanitizeFloat(DurationValue);
				return true;
			}
		}
	}

	return false;
}

bool FNodePropertyManager::SetVariableNodeProperty(
	UK2Node* VarNode,
	const FString& PropertyName,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!VarNode || !Value.IsValid())
	{
		return false;
	}

	// Handle "variable_name" property
	if (PropertyName.Equals(TEXT("variable_name"), ESearchCase::IgnoreCase))
	{
		FString VarName;
		if (Value->TryGetString(VarName))
		{
			UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(VarNode);
			if (VarGet)
			{
				VarGet->VariableReference.SetSelfMember(FName(*VarName));
				VarGet->ReconstructNode();
				return true;
			}

			UK2Node_VariableSet* VarSet = Cast<UK2Node_VariableSet>(VarNode);
			if (VarSet)
			{
				VarSet->VariableReference.SetSelfMember(FName(*VarName));
				VarSet->ReconstructNode();
				return true;
			}
		}
	}

	return false;
}

bool FNodePropertyManager::SetGenericNodeProperty(
	UEdGraphNode* Node,
	const FString& PropertyName,
	const TSharedPtr<FJsonValue>& Value)
{
	if (!Node || !Value.IsValid())
	{
		return false;
	}

	// Handle "pos_x" property
	if (PropertyName.Equals(TEXT("pos_x"), ESearchCase::IgnoreCase))
	{
		double PosX;
		if (Value->TryGetNumber(PosX))
		{
			Node->NodePosX = static_cast<int32>(PosX);
			return true;
		}
	}

	// Handle "pos_y" property
	if (PropertyName.Equals(TEXT("pos_y"), ESearchCase::IgnoreCase))
	{
		double PosY;
		if (Value->TryGetNumber(PosY))
		{
			Node->NodePosY = static_cast<int32>(PosY);
			return true;
		}
	}

	// Handle "comment" property
	if (PropertyName.Equals(TEXT("comment"), ESearchCase::IgnoreCase))
	{
		FString Comment;
		if (Value->TryGetString(Comment))
		{
			Node->NodeComment = Comment;
			return true;
		}
	}

	return false;
}

UEdGraph* FNodePropertyManager::GetGraph(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// If no function name, return EventGraph
	if (FunctionName.IsEmpty())
	{
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
		return nullptr;
	}

	// Search in function graphs
	for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
	{
		if (FuncGraph && FuncGraph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			return FuncGraph;
		}
	}

	return nullptr;
}

UEdGraphNode* FNodePropertyManager::FindNodeByID(UEdGraph* Graph, const FString& NodeID)
{
	if (!Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Try matching by NodeGuid
		if (Node->NodeGuid.ToString().Equals(NodeID, ESearchCase::IgnoreCase))
		{
			return Node;
		}

		// Try matching by GetName()
		if (Node->GetName().Equals(NodeID, ESearchCase::IgnoreCase))
		{
			return Node;
		}
	}

	return nullptr;
}

UBlueprint* FNodePropertyManager::LoadBlueprint(const FString& BlueprintName)
{
	// Try direct path first
	FString BlueprintPath = BlueprintName;

	// If no path prefix, assume /Game/Blueprints/
	if (!BlueprintPath.StartsWith(TEXT("/")))
	{
		BlueprintPath = TEXT("/Game/Blueprints/") + BlueprintName;
	}

	// Add .Blueprint suffix if not present
	if (!BlueprintPath.Contains(TEXT(".")))
	{
		BlueprintPath += TEXT(".") + FPaths::GetBaseFilename(BlueprintPath);
	}

	// Try to load the Blueprint
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

	// If not found, try with UEditorAssetLibrary
	if (!BP)
	{
		FString AssetPath = BlueprintPath;
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
			BP = Cast<UBlueprint>(Asset);
		}
	}

	return BP;
}

TSharedPtr<FJsonObject> FNodePropertyManager::CreateSuccessResponse(const FString& PropertyName)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("updated_property"), PropertyName);
	return Response;
}

TSharedPtr<FJsonObject> FNodePropertyManager::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	return Response;
}
