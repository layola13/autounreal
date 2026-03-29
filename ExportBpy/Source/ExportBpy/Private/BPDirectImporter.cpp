// Copyright sonygodx@gmail.com. All Rights Reserved.

#include "BPDirectImporter.h"

#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
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
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Engine/EngineTypes.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "EditorAssetLibrary.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
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

	return Cast<TObject>(StaticLoadObject(TObject::StaticClass(), nullptr, *Name));
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

	if (NodeClassName.StartsWith(TEXT("K2Node_")))
	{
		return ResolveNamedObject_ImportBpy<UClass>(FString::Printf(TEXT("/Script/BlueprintGraph.%s"), *NodeClassName));
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
	if (GraphType == TEXT("event_graph"))
	{
		return 0;
	}
	if (GraphType == TEXT("function"))
	{
		return 1;
	}
	if (GraphType == TEXT("macro"))
	{
		return 2;
	}
	return 3;
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

	if (OutGraphType == TEXT("function"))
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

	if (OutGraphType == TEXT("function"))
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
			else
			{
				Pin->DefaultObject = DefaultObject;
			}
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

		if (FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Entry.Key)))
		{
			ApplyJsonValueToProperty_ImportBpy(Object, Property, Entry.Value);
		}
	}
}

USCS_Node* FindComponentNodeByName_ImportBpy(UBlueprint* BP, const FString& ComponentName)
{
	if (!BP || !BP->SimpleConstructionScript || ComponentName.IsEmpty())
	{
		return nullptr;
	}

	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::CaseSensitive))
		{
			return Node;
		}
	}

	return nullptr;
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

	if (ParentName.IsEmpty())
	{
		if (!BP->SimpleConstructionScript->GetAllNodes().Contains(Node))
		{
			BP->SimpleConstructionScript->AddNode(Node);
		}
		return true;
	}

	if (USCS_Node* const* ParentNodePtr = KnownNodes.Find(ParentName))
	{
		if (USCS_Node* ParentNode = *ParentNodePtr)
		{
			ParentNode->AddChildNode(Node);
			return true;
		}
	}

	if (USCS_Node* ParentNode = FindComponentNodeByName_ImportBpy(BP, ParentName))
	{
		ParentNode->AddChildNode(Node);
		return true;
	}

	if (USceneComponent* ParentSceneComponent = ResolveNamedObject_ImportBpy<USceneComponent>(ParentName))
	{
		Node->SetParent(ParentSceneComponent);
		if (!BP->SimpleConstructionScript->GetAllNodes().Contains(Node))
		{
			BP->SimpleConstructionScript->AddNode(Node);
		}
		return true;
	}

	OutError = FString::Printf(TEXT("Parent component not found: %s"), *ParentName);
	return false;
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

		for (int32 Index = PendingComponents.Num() - 1; Index >= 0; --Index)
		{
			const TSharedPtr<FJsonObject>& ComponentJson = PendingComponents[Index];
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
			if (!ParentName.IsEmpty() && ParentName != ComponentName && !KnownNodes.Contains(ParentName))
			{
				continue;
			}

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

void ApplyNodeProps_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!Node || !NodeJson.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject>* NodePropsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("node_props"), NodePropsObj) || !NodePropsObj->IsValid())
	{
		return;
	}

	bool bNeedsReconstruct = false;
	bool bApplySelectIndexTypePostReconstruct = false;
	FEdGraphPinType SelectIndexPinType;

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
}

void ApplyPinDefaults_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!Node || !NodeJson.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsObj) || !DefaultsObj->IsValid())
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*DefaultsObj)->Values)
	{
		const FString RequestedPinName = ResolveNodePinAlias_ImportBpy(NodeJson, Entry.Key);
		if (UEdGraphPin* Pin = FindPinFlexible_ImportBpy(Node, RequestedPinName, EGPD_Input))
		{
			ApplyDefaultToPin_ImportBpy(Pin, Entry.Value);
		}
	}
}

void ApplyPinIds_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!Node || !NodeJson.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject>* PinIdsObj = nullptr;
	if (!NodeJson->TryGetObjectField(TEXT("pin_ids"), PinIdsObj) || !PinIdsObj->IsValid())
	{
		return;
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
			continue;
		}

		FGuid ParsedGuid;
		if (TryParseGuid_ImportBpy(Entry.Value->AsString(), ParsedGuid))
		{
			Pin->PinId = ParsedGuid;
		}
	}
}

void ApplyNodeJsonToNode_ImportBpy(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!Node || !NodeJson.IsValid())
	{
		return;
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

	ApplyNodeProps_ImportBpy(Node, NodeJson);
	ApplyPinDefaults_ImportBpy(Node, NodeJson);
	ApplyPinIds_ImportBpy(Node, NodeJson);
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

	const bool bIsFunctionGraph = BP->FunctionGraphs.Contains(Graph);
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

bool UBPDirectImporter::ImportBlueprintFromJson(
	const FString& JsonData,
	const FString& TargetAssetPath,
	bool bCompileBlueprint,
	FString& OutError)
{
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

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		*AssetName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());

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

	FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType, DefaultVal);
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

	if (GraphType == TEXT("function"))
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

	if (GraphType == TEXT("function"))
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
	if (GraphType == TEXT("function"))
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
			if (NodeClass == TEXT("K2Node_FunctionEntry") && GraphType == TEXT("function"))
			{
				TArray<UK2Node_FunctionEntry*> EntryNodes;
				Graph->GetNodesOfClass(EntryNodes);
				Node = EntryNodes.Num() > 0 ? EntryNodes[0] : nullptr;
				ApplyNodeJsonToNode_ImportBpy(Node, NodeObj);
			}
			else if (NodeClass == TEXT("K2Node_FunctionResult") && GraphType == TEXT("function"))
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
				ApplyNodeJsonToNode_ImportBpy(Node, NodeObj);
			}
			else
			{
				Node = CreateNode(Graph, NodeObj, OutError);
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
			ApplyNodeJsonToNode_ImportBpy(*ExistingNode, NodeObj);
		}
	}

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
			if (!SrcNodePtr || !DstNodePtr) continue;

			ConnectPins(*SrcNodePtr, SrcPin, SrcPinFull, SrcPinId, *DstNodePtr, DstPin, DstPinFull, DstPinId);
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
		Result = CreateVariableNode(Graph, NodeJson, true);
	}
	// ── Variable Set ─────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_VariableSet"))
	{
		Result = CreateVariableNode(Graph, NodeJson, false);
	}
	// ── Branch ───────────────────────────────────────────────────────
	else if (NodeClass == TEXT("K2Node_IfThenElse"))
	{
		Result = CreateBranchNode(Graph);
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
		CastNode->CreateNewGuid();
		CastNode->PostPlacedNewNode();
		CastNode->AllocateDefaultPins();
		Graph->AddNode(CastNode, false, false);
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

	ApplyNodeJsonToNode_ImportBpy(Result, NodeJson);

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
	bool bIsGet)
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

	auto ConfigureVariableReference = [&](UK2Node_Variable* Node)
	{
		if (!Node)
		{
			return;
		}

		if (VariableScope.Equals(TEXT("Local"), ESearchCase::IgnoreCase))
		{
			const FString ScopeName = !VariableScopeName.IsEmpty()
				? VariableScopeName
				: FBlueprintEditorUtils::GetTopLevelGraph(Graph)->GetName();
			Node->VariableReference.SetLocalMember(FName(*VarName), ScopeName, bHasVariableGuid ? VariableGuid : FGuid());
			return;
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
				return;
			}
		}

		if (bHasVariableGuid)
		{
			Node->VariableReference.SetSelfMember(FName(*VarName), VariableGuid);
		}
		else
		{
			Node->VariableReference.SetSelfMember(FName(*VarName));
		}
	};

	if (bIsGet)
	{
		UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
		ConfigureVariableReference(Node);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Graph->AddNode(Node, false, false);
		return Node;
	}
	else
	{
		UK2Node_VariableSet* Node = NewObject<UK2Node_VariableSet>(Graph);
		ConfigureVariableReference(Node);
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

void UBPDirectImporter::ConnectPins(
	UEdGraphNode* SrcNode,
	const FString& SrcPinName,
	const FString& SrcPinFullName,
	const FString& SrcPinId,
	UEdGraphNode* DstNode,
	const FString& DstPinName,
	const FString& DstPinFullName,
	const FString& DstPinId)
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
		return;
	}

	const UEdGraphSchema* Schema = SrcPin->GetSchema();
	if (Schema && Schema->TryCreateConnection(SrcPin, DstPin))
	{
		return;
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
	// Load the existing asset
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Cannot load asset: %s"), *AssetPath);
		return false;
	}

	// Parse the JSON properties dict
	TSharedPtr<FJsonObject> PropsObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObj) || !PropsObj.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to parse PropertiesJson for asset: %s"), *AssetPath);
		return false;
	}

	// Apply properties using the shared reflection helper (same as component property import)
	ApplyJsonObjectToObject_ImportBpy(Asset, PropsObj);

	// Mark dirty and save
	UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Asset has no outer package.");
		return false;
	}

	Package->MarkPackageDirty();

	const FString PackageName     = Package->GetName();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags     = SAVE_None;

	if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save asset package: %s"), *PackageFileName);
		return false;
	}

	return true;
}
