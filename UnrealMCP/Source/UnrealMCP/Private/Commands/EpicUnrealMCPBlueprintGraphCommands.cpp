#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/BlueprintGraph/NodeManager.h"
#include "Commands/BlueprintGraph/BPConnector.h"
#include "Commands/BlueprintGraph/BPVariables.h"
#include "Commands/BlueprintGraph/EventManager.h"
#include "Commands/BlueprintGraph/NodeDeleter.h"
#include "Commands/BlueprintGraph/NodePropertyManager.h"
#include "Commands/BlueprintGraph/GraphFormatter.h"
#include "Commands/BlueprintGraph/Function/FunctionManager.h"
#include "Commands/BlueprintGraph/Function/FunctionIO.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString TrimmedString_BPGC(const FString& InValue)
{
    FString Value = InValue;
    Value.TrimStartAndEndInline();
    return Value;
}

TSharedPtr<FJsonObject> CloneJsonObject_BPGC(const TSharedPtr<FJsonObject>& Source)
{
    TSharedPtr<FJsonObject> Clone = MakeShared<FJsonObject>();
    if (!Source.IsValid())
    {
        return Clone;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Source->Values)
    {
        Clone->SetField(Entry.Key, Entry.Value);
    }

    return Clone;
}

TSharedPtr<FJsonObject> MergeBatchOperationParams_BPGC(
    const TSharedPtr<FJsonObject>& BatchParams,
    const TSharedPtr<FJsonObject>& OperationParams)
{
    TSharedPtr<FJsonObject> MergedParams = CloneJsonObject_BPGC(OperationParams);

    FString BlueprintName;
    if (!MergedParams->TryGetStringField(TEXT("blueprint_name"), BlueprintName) && BatchParams.IsValid())
    {
        if (BatchParams->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
        {
            MergedParams->SetStringField(TEXT("blueprint_name"), BlueprintName);
        }
    }

    FString FunctionName;
    if (!MergedParams->TryGetStringField(TEXT("function_name"), FunctionName) && BatchParams.IsValid() && BatchParams->HasField(TEXT("function_name")))
    {
        MergedParams->SetField(TEXT("function_name"), BatchParams->Values.FindRef(TEXT("function_name")));
    }

    return MergedParams;
}

FString ResolveBlueprintReference_BPGC(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FString();
    }

    FString BlueprintRef;
    Params->TryGetStringField(TEXT("blueprint_name"), BlueprintRef);
    if (BlueprintRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("soft_path_from_project_root"), BlueprintRef);
    }
    if (BlueprintRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("target"), BlueprintRef);
    }
    if (BlueprintRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("asset_path"), BlueprintRef);
    }

    BlueprintRef.TrimStartAndEndInline();
    return BlueprintRef;
}

bool IsEventGraph_BPGC(const FString& StrandName, const FString& GraphType)
{
    const FString TrimmedStrandName = TrimmedString_BPGC(StrandName);
    const FString TrimmedGraphType = TrimmedString_BPGC(GraphType);

    if (TrimmedGraphType.Equals(TEXT("Function"), ESearchCase::IgnoreCase) ||
        TrimmedGraphType.Equals(TEXT("Macro"), ESearchCase::IgnoreCase))
    {
        return false;
    }

    return TrimmedStrandName.IsEmpty() ||
        TrimmedStrandName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase) ||
        TrimmedStrandName.Equals(TEXT("UbergraphPage"), ESearchCase::IgnoreCase) ||
        TrimmedStrandName.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase) ||
        TrimmedGraphType.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase) ||
        TrimmedGraphType.Equals(TEXT("Event"), ESearchCase::IgnoreCase) ||
        TrimmedGraphType.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase);
}

void ApplyBlueprintAndStrand_BPGC(
    const TSharedPtr<FJsonObject>& BatchParams,
    const TSharedPtr<FJsonObject>& OperationParams)
{
    if (!OperationParams.IsValid())
    {
        return;
    }

    FString BlueprintRef;
    if (!OperationParams->TryGetStringField(TEXT("blueprint_name"), BlueprintRef))
    {
        BlueprintRef = ResolveBlueprintReference_BPGC(BatchParams);
        if (!BlueprintRef.IsEmpty())
        {
            OperationParams->SetStringField(TEXT("blueprint_name"), BlueprintRef);
        }
    }

    const bool bBatchSpecifiesStrand = BatchParams.IsValid() &&
        (BatchParams->HasField(TEXT("strand_name")) || BatchParams->HasField(TEXT("graph_type")));

    FString StrandName;
    if (!OperationParams->TryGetStringField(TEXT("strand_name"), StrandName) && BatchParams.IsValid())
    {
        BatchParams->TryGetStringField(TEXT("strand_name"), StrandName);
    }

    FString GraphType;
    if (!OperationParams->TryGetStringField(TEXT("graph_type"), GraphType) && BatchParams.IsValid())
    {
        BatchParams->TryGetStringField(TEXT("graph_type"), GraphType);
    }

    if (!bBatchSpecifiesStrand)
    {
        return;
    }

    if (!IsEventGraph_BPGC(StrandName, GraphType))
    {
        OperationParams->SetStringField(TEXT("function_name"), StrandName);
    }
    else
    {
        OperationParams->RemoveField(TEXT("function_name"));
    }
}

void ApplyNodePosition_BPGC(const TSharedPtr<FJsonObject>& SourceNode, const TSharedPtr<FJsonObject>& TargetParams)
{
    if (!SourceNode.IsValid() || !TargetParams.IsValid())
    {
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* NodePosition = nullptr;
    if (!SourceNode->TryGetArrayField(TEXT("node_position"), NodePosition) || !NodePosition || NodePosition->Num() < 2)
    {
        return;
    }

    double X = 0.0;
    double Y = 0.0;
    if ((*NodePosition)[0].IsValid())
    {
        (*NodePosition)[0]->TryGetNumber(X);
    }
    if ((*NodePosition)[1].IsValid())
    {
        (*NodePosition)[1]->TryGetNumber(Y);
    }

    TargetParams->SetNumberField(TEXT("pos_x"), X);
    TargetParams->SetNumberField(TEXT("pos_y"), Y);
}

FString NormalizeAuraEventType_BPGC(const FString& EventName)
{
    FString Normalized = TrimmedString_BPGC(EventName);
    if (Normalized.StartsWith(TEXT("Receive"), ESearchCase::IgnoreCase))
    {
        Normalized.RightChopInline(7, EAllowShrinking::No);
    }

    if (Normalized.Equals(TEXT("BeginPlay"), ESearchCase::IgnoreCase))
    {
        return TEXT("BeginPlay");
    }
    if (Normalized.Equals(TEXT("Tick"), ESearchCase::IgnoreCase))
    {
        return TEXT("Tick");
    }

    return Normalized;
}

FString MapAuraNodeType_BPGC(const FString& NodeType)
{
    const FString TrimmedType = TrimmedString_BPGC(NodeType);
    if (TrimmedType.IsEmpty())
    {
        return FString();
    }

    static const TMap<FString, FString> AuraToLegacyTypeMap = {
        {TEXT("K2Node_Event"), TEXT("Event")},
        {TEXT("K2Node_CallFunction"), TEXT("CallFunction")},
        {TEXT("K2Node_VariableGet"), TEXT("VariableGet")},
        {TEXT("K2Node_VariableSet"), TEXT("VariableSet")},
        {TEXT("K2Node_Self"), TEXT("Self")},
        {TEXT("K2Node_InputAction"), TEXT("EnhancedInputAction")},
        {TEXT("K2Node_Knot"), TEXT("Knot")},
        {TEXT("K2Node_ExecutionSequence"), TEXT("ExecutionSequence")},
        {TEXT("K2Node_IfThenElse"), TEXT("Branch")}
    };

    if (const FString* MappedType = AuraToLegacyTypeMap.Find(TrimmedType))
    {
        return *MappedType;
    }

    return TrimmedType;
}

UClass* ResolveClassByName_BPGC(const FString& ClassName)
{
    const FString TrimmedClassName = TrimmedString_BPGC(ClassName);
    if (TrimmedClassName.IsEmpty())
    {
        return nullptr;
    }

    if (TrimmedClassName.StartsWith(TEXT("/")))
    {
        return Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *TrimmedClassName));
    }

    if (UClass* FoundClass = FindFirstObject<UClass>(*TrimmedClassName))
    {
        return FoundClass;
    }

    if (!TrimmedClassName.StartsWith(TEXT("A")) && !TrimmedClassName.StartsWith(TEXT("U")))
    {
        if (UClass* ActorClass = FindFirstObject<UClass>(*(TEXT("A") + TrimmedClassName)))
        {
            return ActorClass;
        }
        if (UClass* UObjectClass = FindFirstObject<UClass>(*(TEXT("U") + TrimmedClassName)))
        {
            return UObjectClass;
        }
    }

    if (TrimmedClassName.Contains(TEXT(".")))
    {
        const FString ScriptPath = FString::Printf(TEXT("/Script/%s"), *TrimmedClassName);
        if (UClass* ScriptClass = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *ScriptPath)))
        {
            return ScriptClass;
        }
    }

    const FString EnginePath = FString::Printf(TEXT("/Script/Engine.%s"), *TrimmedClassName);
    if (UClass* EngineClass = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *EnginePath)))
    {
        return EngineClass;
    }

    return nullptr;
}

FString ResolveDefaultObjectPath_BPGC(const FString& PinName, const FString& RawValue)
{
    FString Value = TrimmedString_BPGC(RawValue);
    if (Value.IsEmpty())
    {
        return Value;
    }

    if (Value.StartsWith(TEXT("/")))
    {
        return Value;
    }

    if (UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(Value))
    {
        return Blueprint->GetPathName();
    }

    if (PinName.EndsWith(TEXT("Class"), ESearchCase::IgnoreCase) ||
        PinName.EndsWith(TEXT("Type"), ESearchCase::IgnoreCase))
    {
        if (UClass* ClassObject = ResolveClassByName_BPGC(Value))
        {
            return ClassObject->GetPathName();
        }
    }

    return Value;
}

FString JsonValueToDefaultString_BPGC(const FString& PinName, const TSharedPtr<FJsonValue>& JsonValue)
{
    if (!JsonValue.IsValid())
    {
        return FString();
    }

    if (JsonValue->Type == EJson::String)
    {
        return JsonValue->AsString();
    }

    if (JsonValue->Type == EJson::Number)
    {
        return FString::SanitizeFloat(JsonValue->AsNumber());
    }

    if (JsonValue->Type == EJson::Boolean)
    {
        return JsonValue->AsBool() ? TEXT("true") : TEXT("false");
    }

    if (JsonValue->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayValues = JsonValue->AsArray();
        if (ArrayValues.Num() == 3)
        {
            double A = 0.0;
            double B = 0.0;
            double C = 0.0;
            if (ArrayValues[0].IsValid() && ArrayValues[1].IsValid() && ArrayValues[2].IsValid() &&
                ArrayValues[0]->TryGetNumber(A) && ArrayValues[1]->TryGetNumber(B) && ArrayValues[2]->TryGetNumber(C))
            {
                if (PinName.Contains(TEXT("rot"), ESearchCase::IgnoreCase))
                {
                    return FString::Printf(TEXT("(Pitch=%s,Yaw=%s,Roll=%s)"), *FString::SanitizeFloat(A), *FString::SanitizeFloat(B), *FString::SanitizeFloat(C));
                }
                return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"), *FString::SanitizeFloat(A), *FString::SanitizeFloat(B), *FString::SanitizeFloat(C));
            }
        }

        if (ArrayValues.Num() == 4 && PinName.Contains(TEXT("color"), ESearchCase::IgnoreCase))
        {
            double R = 0.0;
            double G = 0.0;
            double B = 0.0;
            double A = 0.0;
            if (ArrayValues[0].IsValid() && ArrayValues[1].IsValid() && ArrayValues[2].IsValid() && ArrayValues[3].IsValid() &&
                ArrayValues[0]->TryGetNumber(R) && ArrayValues[1]->TryGetNumber(G) && ArrayValues[2]->TryGetNumber(B) && ArrayValues[3]->TryGetNumber(A))
            {
                return FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"), *FString::SanitizeFloat(R), *FString::SanitizeFloat(G), *FString::SanitizeFloat(B), *FString::SanitizeFloat(A));
            }
        }
    }

    FString SerializedValue;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SerializedValue);
    FJsonSerializer::Serialize(JsonValue.ToSharedRef(), TEXT(""), Writer);
    return SerializedValue;
}

TArray<TSharedPtr<FJsonValue>> ConvertAuraParamsToPinDefaults_BPGC(const TSharedPtr<FJsonObject>& AuraParams)
{
    TArray<TSharedPtr<FJsonValue>> PinDefaults;
    if (!AuraParams.IsValid())
    {
        return PinDefaults;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : AuraParams->Values)
    {
        if (!Entry.Value.IsValid())
        {
            continue;
        }

        TSharedPtr<FJsonObject> PinDefault = MakeShared<FJsonObject>();
        PinDefault->SetStringField(TEXT("pin_name"), Entry.Key);

        if (Entry.Value->Type == EJson::String)
        {
            const FString RawString = Entry.Value->AsString();
            const FString ResolvedObjectPath = ResolveDefaultObjectPath_BPGC(Entry.Key, RawString);
            const bool bTreatAsObject = ResolvedObjectPath.StartsWith(TEXT("/")) ||
                Entry.Key.EndsWith(TEXT("Class"), ESearchCase::IgnoreCase) ||
                Entry.Key.EndsWith(TEXT("Object"), ESearchCase::IgnoreCase) ||
                Entry.Key.EndsWith(TEXT("Asset"), ESearchCase::IgnoreCase);

            if (bTreatAsObject)
            {
                PinDefault->SetStringField(TEXT("default_object_path"), ResolvedObjectPath);
                PinDefault->SetStringField(TEXT("value_kind"), Entry.Key.EndsWith(TEXT("Class"), ESearchCase::IgnoreCase) ? TEXT("class") : TEXT("object"));
            }
            else
            {
                PinDefault->SetStringField(TEXT("default_value"), RawString);
            }
        }
        else
        {
            PinDefault->SetStringField(TEXT("default_value"), JsonValueToDefaultString_BPGC(Entry.Key, Entry.Value));
        }

        PinDefaults.Add(MakeShared<FJsonValueObject>(PinDefault));
    }

    return PinDefaults;
}

TSharedPtr<FJsonObject> BuildAddNodeParamsFromAura_BPGC(
    const TSharedPtr<FJsonObject>& BatchParams,
    const TSharedPtr<FJsonObject>& AuraNode,
    FString& OutError)
{
    if (!AuraNode.IsValid())
    {
        OutError = TEXT("Node entry must be an object");
        return nullptr;
    }

    TSharedPtr<FJsonObject> AddParams = CloneJsonObject_BPGC(AuraNode);
    ApplyBlueprintAndStrand_BPGC(BatchParams, AddParams);
    ApplyNodePosition_BPGC(AuraNode, AddParams);

    FString NodeType;
    if (!AuraNode->TryGetStringField(TEXT("node_type"), NodeType))
    {
        OutError = TEXT("Missing 'node_type' parameter");
        return nullptr;
    }

    AddParams->SetStringField(TEXT("node_type"), MapAuraNodeType_BPGC(NodeType));

    const TSharedPtr<FJsonObject>* NodeProperties = nullptr;
    if (AuraNode->TryGetObjectField(TEXT("node_properties"), NodeProperties) && NodeProperties && NodeProperties->IsValid())
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*NodeProperties)->Values)
        {
            AddParams->SetField(Entry.Key, Entry.Value);
        }
    }

    const FString LegacyNodeType = AddParams->GetStringField(TEXT("node_type"));
    if (LegacyNodeType.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
    {
        FString EventName;
        if (AuraNode->TryGetStringField(TEXT("event_name"), EventName))
        {
            AddParams->SetStringField(TEXT("event_type"), NormalizeAuraEventType_BPGC(EventName));
        }
    }
    else if (LegacyNodeType.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase))
    {
        FString FunctionName;
        if (AuraNode->TryGetStringField(TEXT("function_name"), FunctionName))
        {
            AddParams->SetStringField(TEXT("target_function"), FunctionName);
        }

        FString TargetClass;
        if (AuraNode->TryGetStringField(TEXT("target"), TargetClass))
        {
            AddParams->SetStringField(TEXT("target_class"), TargetClass);
        }

        const TSharedPtr<FJsonObject>* AuraParams = nullptr;
        if (AuraNode->TryGetObjectField(TEXT("params"), AuraParams) && AuraParams && AuraParams->IsValid())
        {
            AddParams->SetArrayField(TEXT("pin_defaults"), ConvertAuraParamsToPinDefaults_BPGC(*AuraParams));
        }
    }
    else if (LegacyNodeType.Equals(TEXT("VariableGet"), ESearchCase::IgnoreCase) ||
             LegacyNodeType.Equals(TEXT("VariableSet"), ESearchCase::IgnoreCase))
    {
        FString VariableName;
        if (!AuraNode->TryGetStringField(TEXT("variable_name"), VariableName))
        {
            AuraNode->TryGetStringField(TEXT("component_name"), VariableName);
        }

        if (!VariableName.IsEmpty())
        {
            AddParams->SetStringField(TEXT("variable_name"), VariableName);
        }
    }
    else if (LegacyNodeType.Equals(TEXT("EnhancedInputAction"), ESearchCase::IgnoreCase))
    {
        FString ActionName;
        if (AuraNode->TryGetStringField(TEXT("action_name"), ActionName))
        {
            AddParams->SetStringField(TEXT("input_action"), ActionName);
        }
    }

    return AddParams;
}

bool ShouldCompileBlueprint_BPGC(const TSharedPtr<FJsonObject>& Params)
{
    bool bCompileBlueprint = true;
    if (Params.IsValid())
    {
        Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);
    }

    return bCompileBlueprint;
}

UBlueprint* LoadBlueprintForAuraGraph_BPGC(const TSharedPtr<FJsonObject>& Params, FString& OutBlueprintRef)
{
    OutBlueprintRef = ResolveBlueprintReference_BPGC(Params);
    if (OutBlueprintRef.IsEmpty())
    {
        return nullptr;
    }

    return FEpicUnrealMCPCommonUtils::FindBlueprint(OutBlueprintRef);
}

UEdGraph* ResolveGraphForAuraStrand_BPGC(
    UBlueprint* Blueprint,
    const FString& StrandName,
    const FString& GraphType)
{
    if (!Blueprint)
    {
        return nullptr;
    }

    if (IsEventGraph_BPGC(StrandName, GraphType))
    {
        return Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0] : nullptr;
    }

    for (UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
    {
        if (FunctionGraph && FunctionGraph->GetName().Equals(StrandName, ESearchCase::IgnoreCase))
        {
            return FunctionGraph;
        }
    }

    for (UEdGraph* MacroGraph : Blueprint->MacroGraphs)
    {
        if (MacroGraph && MacroGraph->GetName().Equals(StrandName, ESearchCase::IgnoreCase))
        {
            return MacroGraph;
        }
    }

    return nullptr;
}

UEdGraphNode* FindNodeByUniqueName_BPGC(UEdGraph* Graph, const FString& NodeUniqueName)
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

        if (Node->NodeGuid.ToString().Equals(NodeUniqueName, ESearchCase::IgnoreCase) ||
            Node->GetName().Equals(NodeUniqueName, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }

    return nullptr;
}

FString NormalizeLoosePinName_BPGC(const FString& InName)
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

        for (const TCHAR Character : Value)
        {
            if (!FChar::IsHexDigit(Character))
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

bool DoesPinNameMatchLoose_BPGC(const UEdGraphPin* Pin, const FString& DesiredName)
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

    if (NormalizeLoosePinName_BPGC(PinName).Equals(NormalizeLoosePinName_BPGC(DesiredName), ESearchCase::IgnoreCase))
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

UEdGraphPin* FindPinRecursiveByLooseName_BPGC(
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

        if (Pin->Direction == Direction && DoesPinNameMatchLoose_BPGC(Pin, DesiredName))
        {
            return Pin;
        }

        if (Pin->SubPins.Num() > 0)
        {
            if (UEdGraphPin* SubPin = FindPinRecursiveByLooseName_BPGC(Pin->SubPins, DesiredName, Direction))
            {
                return SubPin;
            }
        }
    }

    return nullptr;
}

UEdGraphPin* FindPinByLooseName_BPGC(
    UEdGraphNode* Node,
    const FString& DesiredName,
    const EEdGraphPinDirection Direction)
{
    return Node ? FindPinRecursiveByLooseName_BPGC(Node->Pins, DesiredName, Direction) : nullptr;
}

TSharedPtr<FJsonObject> CompileBlueprint_BPGC(const FString& BlueprintName)
{
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), Blueprint->Status != BS_Error);
    Result->SetBoolField(TEXT("compiled"), Blueprint->Status != BS_Error);
    Result->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(static_cast<int64>(Blueprint->Status)));
    return Result;
}
}

FEpicUnrealMCPBlueprintGraphCommands::FEpicUnrealMCPBlueprintGraphCommands()
{
}

FEpicUnrealMCPBlueprintGraphCommands::~FEpicUnrealMCPBlueprintGraphCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("add_blueprint_node"))
    {
        return HandleAddBlueprintNode(Params);
    }
    else if (CommandType == TEXT("connect_nodes"))
    {
        return HandleConnectNodes(Params);
    }
    else if (CommandType == TEXT("create_variable"))
    {
        return HandleCreateVariable(Params);
    }
    else if (CommandType == TEXT("set_blueprint_variable_properties"))
    {
        return HandleSetVariableProperties(Params);
    }
    else if (CommandType == TEXT("add_event_node"))
    {
        return HandleAddEventNode(Params);
    }
    else if (CommandType == TEXT("delete_node"))
    {
        return HandleDeleteNode(Params);
    }
    else if (CommandType == TEXT("set_node_property"))
    {
        return HandleSetNodeProperty(Params);
    }
    else if (CommandType == TEXT("add_nodes_batch"))
    {
        return HandleAddNodesBatch(Params);
    }
    else if (CommandType == TEXT("connect_nodes_batch"))
    {
        return HandleConnectNodesBatch(Params);
    }
    else if (CommandType == TEXT("disconnect_nodes_batch"))
    {
        return HandleDisconnectNodesBatch(Params);
    }
    else if (CommandType == TEXT("remove_nodes_batch"))
    {
        return HandleRemoveNodesBatch(Params);
    }
    else if (CommandType == TEXT("set_node_pin_defaults_batch"))
    {
        return HandleSetNodePinDefaultsBatch(Params);
    }
    else if (CommandType == TEXT("add_blueprint_node_to_strand"))
    {
        return HandleAddBlueprintNodeToStrand(Params);
    }
    else if (CommandType == TEXT("connect_blueprint_nodes"))
    {
        return HandleConnectBlueprintNodes(Params);
    }
    else if (CommandType == TEXT("disconnect_blueprint_nodes"))
    {
        return HandleDisconnectBlueprintNodes(Params);
    }
    else if (CommandType == TEXT("remove_blueprint_nodes"))
    {
        return HandleRemoveBlueprintNodes(Params);
    }
    else if (CommandType == TEXT("set_node_pins_defaults"))
    {
        return HandleSetNodePinsDefaults(Params);
    }
    else if (CommandType == TEXT("create_function"))
    {
        return HandleCreateFunction(Params);
    }
    else if (CommandType == TEXT("add_function_input"))
    {
        return HandleAddFunctionInput(Params);
    }
    else if (CommandType == TEXT("add_function_output"))
    {
        return HandleAddFunctionOutput(Params);
    }
    else if (CommandType == TEXT("delete_function"))
    {
        return HandleDeleteFunction(Params);
    }
    else if (CommandType == TEXT("rename_function"))
    {
        return HandleRenameFunction(Params);
    }
    else if (CommandType == TEXT("format_graph"))
    {
        return HandleFormatGraph(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint graph command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddBlueprintNode(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeType;
    if (!Params->TryGetStringField(TEXT("node_type"), NodeType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_type' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddBlueprintNode: Adding %s node to blueprint '%s'"), *NodeType, *BlueprintName);

    // Use the NodeManager to add the node
    return FBlueprintNodeManager::AddNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleConnectNodes(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString SourceNodeId;
    if (!Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_node_id' parameter"));
    }

    FString SourcePinName;
    if (!Params->TryGetStringField(TEXT("source_pin_name"), SourcePinName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_pin_name' parameter"));
    }

    FString TargetNodeId;
    if (!Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_node_id' parameter"));
    }

    FString TargetPinName;
    if (!Params->TryGetStringField(TEXT("target_pin_name"), TargetPinName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_pin_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleConnectNodes: Connecting %s.%s to %s.%s in blueprint '%s'"),
        *SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName, *BlueprintName);

    // Use the BPConnector to connect the nodes
    return FBPConnector::ConnectNodes(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCreateVariable(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    FString VariableType;
    if (!Params->TryGetStringField(TEXT("variable_type"), VariableType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_type' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleCreateVariable: Creating %s variable '%s' in blueprint '%s'"),
        *VariableType, *VariableName, *BlueprintName);

    // Use the BPVariables to create the variable
    return FBPVariables::CreateVariable(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleSetVariableProperties: Modifying variable '%s' in blueprint '%s'"),
        *VariableName, *BlueprintName);

    // Use the BPVariables to set the variable properties
    return FBPVariables::SetVariableProperties(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddEventNode(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString EventName;
    if (!Params->TryGetStringField(TEXT("event_name"), EventName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddEventNode: Adding event '%s' to blueprint '%s'"),
        *EventName, *BlueprintName);

    // Use the EventManager to add the event node
    return FEventManager::AddEventNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeID;
    if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    }

    UE_LOG(LogTemp, Display,
        TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteNode: Deleting node '%s' from blueprint '%s'"),
        *NodeID, *BlueprintName);

    return FNodeDeleter::DeleteNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeID;
    if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    }

    // Check if this is semantic mode (action parameter) or legacy mode (property_name)
    bool bHasAction = Params->HasField(TEXT("action"));

    if (bHasAction)
    {
        // Semantic mode - delegate directly to SetNodeProperty
        FString Action;
        Params->TryGetStringField(TEXT("action"), Action);
        UE_LOG(LogTemp, Display,
            TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodeProperty: Semantic mode - action '%s' on node '%s' in blueprint '%s'"),
            *Action, *NodeID, *BlueprintName);
    }
    else
    {
        // Legacy mode - require property_name
        FString PropertyName;
        if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
        }

        UE_LOG(LogTemp, Display,
            TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodeProperty: Legacy mode - Setting '%s' on node '%s' in blueprint '%s'"),
            *PropertyName, *NodeID, *BlueprintName);
    }

    return FNodePropertyManager::SetNodeProperty(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddNodesBatch(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!Params->TryGetArrayField(TEXT("nodes"), Nodes))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'nodes' parameter"));
    }

    bool bContinueOnError = false;
    Params->TryGetBoolField(TEXT("continue_on_error"), bContinueOnError);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (int32 Index = 0; Index < Nodes->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& NodeValue = (*Nodes)[Index];
        if (!NodeValue.IsValid() || NodeValue->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("nodes[%d]: entry must be an object"), Index)));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        TSharedPtr<FJsonObject> OperationParams = MergeBatchOperationParams_BPGC(Params, NodeValue->AsObject());
        TSharedPtr<FJsonObject> OperationResult = HandleAddBlueprintNode(OperationParams);
        const bool bSuccess = OperationResult.IsValid() && (!OperationResult->HasField(TEXT("success")) || OperationResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("nodes[%d]: %s"), Index, OperationResult.IsValid() ? *OperationResult->GetStringField(TEXT("error")) : TEXT("operation failed"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        Results.Add(MakeShared<FJsonValueObject>(OperationResult));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("results"), Results);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("success_count"), Results.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());

    if (bCompileBlueprint && Results.Num() > 0)
    {
        ResultObj->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintName));
    }

    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleConnectNodesBatch(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
    if (!Params->TryGetArrayField(TEXT("connections"), Connections))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'connections' parameter"));
    }

    bool bContinueOnError = false;
    Params->TryGetBoolField(TEXT("continue_on_error"), bContinueOnError);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (int32 Index = 0; Index < Connections->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& ConnectionValue = (*Connections)[Index];
        if (!ConnectionValue.IsValid() || ConnectionValue->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connections[%d]: entry must be an object"), Index)));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        TSharedPtr<FJsonObject> OperationParams = MergeBatchOperationParams_BPGC(Params, ConnectionValue->AsObject());
        OperationParams->SetBoolField(TEXT("compile_blueprint"), false);

        TSharedPtr<FJsonObject> OperationResult = HandleConnectNodes(OperationParams);
        const bool bSuccess = OperationResult.IsValid() && (!OperationResult->HasField(TEXT("success")) || OperationResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("connections[%d]: %s"), Index, OperationResult.IsValid() ? *OperationResult->GetStringField(TEXT("error")) : TEXT("operation failed"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        Results.Add(MakeShared<FJsonValueObject>(OperationResult));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("results"), Results);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("success_count"), Results.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());

    if (bCompileBlueprint && Results.Num() > 0)
    {
        ResultObj->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintName));
    }

    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDisconnectNodesBatch(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Disconnects = nullptr;
    if (!Params->TryGetArrayField(TEXT("disconnects"), Disconnects))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'disconnects' parameter"));
    }

    bool bContinueOnError = false;
    Params->TryGetBoolField(TEXT("continue_on_error"), bContinueOnError);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (int32 Index = 0; Index < Disconnects->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& DisconnectValue = (*Disconnects)[Index];
        if (!DisconnectValue.IsValid() || DisconnectValue->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("disconnects[%d]: entry must be an object"), Index)));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        TSharedPtr<FJsonObject> OperationParams = MergeBatchOperationParams_BPGC(Params, DisconnectValue->AsObject());
        OperationParams->SetStringField(TEXT("action"), TEXT("disconnect_pin"));

        TSharedPtr<FJsonObject> OperationResult = HandleSetNodeProperty(OperationParams);
        const bool bSuccess = OperationResult.IsValid() && (!OperationResult->HasField(TEXT("success")) || OperationResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("disconnects[%d]: %s"), Index, OperationResult.IsValid() ? *OperationResult->GetStringField(TEXT("error")) : TEXT("operation failed"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        Results.Add(MakeShared<FJsonValueObject>(OperationResult));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("results"), Results);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("success_count"), Results.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());

    if (bCompileBlueprint && Results.Num() > 0)
    {
        ResultObj->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintName));
    }

    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleRemoveNodesBatch(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!Params->TryGetArrayField(TEXT("nodes"), Nodes))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'nodes' parameter"));
    }

    bool bContinueOnError = false;
    Params->TryGetBoolField(TEXT("continue_on_error"), bContinueOnError);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (int32 Index = 0; Index < Nodes->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& NodeValue = (*Nodes)[Index];
        if (!NodeValue.IsValid() || NodeValue->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("nodes[%d]: entry must be an object"), Index)));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        TSharedPtr<FJsonObject> OperationParams = MergeBatchOperationParams_BPGC(Params, NodeValue->AsObject());
        TSharedPtr<FJsonObject> OperationResult = HandleDeleteNode(OperationParams);
        const bool bSuccess = OperationResult.IsValid() && (!OperationResult->HasField(TEXT("success")) || OperationResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("nodes[%d]: %s"), Index, OperationResult.IsValid() ? *OperationResult->GetStringField(TEXT("error")) : TEXT("operation failed"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        Results.Add(MakeShared<FJsonValueObject>(OperationResult));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("results"), Results);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("success_count"), Results.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());

    if (bCompileBlueprint && Results.Num() > 0)
    {
        ResultObj->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintName));
    }

    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodePinDefaultsBatch(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* PinDefaults = nullptr;
    if (!Params->TryGetArrayField(TEXT("pin_defaults"), PinDefaults))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pin_defaults' parameter"));
    }

    bool bContinueOnError = false;
    Params->TryGetBoolField(TEXT("continue_on_error"), bContinueOnError);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (int32 Index = 0; Index < PinDefaults->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& PinDefaultValue = (*PinDefaults)[Index];
        if (!PinDefaultValue.IsValid() || PinDefaultValue->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("pin_defaults[%d]: entry must be an object"), Index)));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        TSharedPtr<FJsonObject> OperationParams = MergeBatchOperationParams_BPGC(Params, PinDefaultValue->AsObject());
        OperationParams->SetStringField(TEXT("action"), TEXT("set_pin_default"));

        TSharedPtr<FJsonObject> OperationResult = HandleSetNodeProperty(OperationParams);
        const bool bSuccess = OperationResult.IsValid() && (!OperationResult->HasField(TEXT("success")) || OperationResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("pin_defaults[%d]: %s"), Index, OperationResult.IsValid() ? *OperationResult->GetStringField(TEXT("error")) : TEXT("operation failed"))));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        Results.Add(MakeShared<FJsonValueObject>(OperationResult));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("results"), Results);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("success_count"), Results.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());

    if (bCompileBlueprint && Results.Num() > 0)
    {
        ResultObj->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintName));
    }

    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddBlueprintNodeToStrand(const TSharedPtr<FJsonObject>& Params)
{
    const FString BlueprintRef = ResolveBlueprintReference_BPGC(Params);
    if (BlueprintRef.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path_from_project_root' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("nodes"), Nodes))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'nodes' parameter"));
    }

    const bool bCompileBlueprint = ShouldCompileBlueprint_BPGC(Params);

    TArray<TSharedPtr<FJsonValue>> SuccessfulNodes;
    TArray<TSharedPtr<FJsonValue>> FailedNodes;

    for (int32 Index = 0; Index < Nodes->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& NodeValue = (*Nodes)[Index];
        if (!NodeValue.IsValid() || NodeValue->Type != EJson::Object)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TEXT("Node entry must be an object"));
            FailedNodes.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        FString TranslationError;
        TSharedPtr<FJsonObject> AddParams = BuildAddNodeParamsFromAura_BPGC(Params, NodeValue->AsObject(), TranslationError);
        if (!AddParams.IsValid())
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TranslationError.IsEmpty() ? TEXT("Failed to translate Aura node") : TranslationError);
            FailedNodes.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        TSharedPtr<FJsonObject> AddResult = HandleAddBlueprintNode(AddParams);
        const bool bSuccess = AddResult.IsValid() && (!AddResult->HasField(TEXT("success")) || AddResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), AddResult.IsValid() && AddResult->HasField(TEXT("error")) ? AddResult->GetStringField(TEXT("error")) : TEXT("Failed to add node"));
            FailedNodes.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        TSharedPtr<FJsonObject> SuccessEntry = MakeShared<FJsonObject>();
        SuccessEntry->SetStringField(TEXT("unique_name"), AddResult->GetStringField(TEXT("node_id")));
        SuccessEntry->SetStringField(TEXT("node_type"), AddResult->GetStringField(TEXT("node_type")));
        SuccessEntry->SetNumberField(TEXT("pos_x"), AddResult->GetNumberField(TEXT("pos_x")));
        SuccessEntry->SetNumberField(TEXT("pos_y"), AddResult->GetNumberField(TEXT("pos_y")));
        SuccessfulNodes.Add(MakeShared<FJsonValueObject>(SuccessEntry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), FailedNodes.Num() == 0);
    Result->SetStringField(TEXT("blueprint_name"), BlueprintRef);
    Result->SetStringField(TEXT("strand_name"), Params.IsValid() && Params->HasField(TEXT("strand_name")) ? Params->GetStringField(TEXT("strand_name")) : TEXT("EventGraph"));
    Result->SetNumberField(TEXT("total_nodes"), Nodes->Num());
    Result->SetNumberField(TEXT("successful_nodes"), SuccessfulNodes.Num());
    Result->SetNumberField(TEXT("failed_nodes"), FailedNodes.Num());
    Result->SetArrayField(TEXT("successful"), SuccessfulNodes);
    Result->SetArrayField(TEXT("failed"), FailedNodes);

    if (bCompileBlueprint && SuccessfulNodes.Num() > 0)
    {
        Result->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintRef));
    }

    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleConnectBlueprintNodes(const TSharedPtr<FJsonObject>& Params)
{
    const FString BlueprintRef = ResolveBlueprintReference_BPGC(Params);
    if (BlueprintRef.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path_from_project_root' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
    if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("connections"), Connections))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'connections' parameter"));
    }

    const bool bCompileBlueprint = ShouldCompileBlueprint_BPGC(Params);
    TArray<TSharedPtr<FJsonValue>> SuccessfulConnections;
    TArray<TSharedPtr<FJsonValue>> FailedConnections;

    for (int32 Index = 0; Index < Connections->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& ConnectionValue = (*Connections)[Index];
        if (!ConnectionValue.IsValid() || ConnectionValue->Type != EJson::Object)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TEXT("Connection entry must be an object"));
            FailedConnections.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        TSharedPtr<FJsonObject> ConnectParams = CloneJsonObject_BPGC(ConnectionValue->AsObject());
        ApplyBlueprintAndStrand_BPGC(Params, ConnectParams);
        ConnectParams->SetBoolField(TEXT("compile_blueprint"), false);

        FString SourceNodeUniqueName;
        FString TargetNodeUniqueName;
        FString SourcePin;
        FString TargetPin;
        if (!ConnectParams->TryGetStringField(TEXT("source_node_unique_name"), SourceNodeUniqueName) ||
            !ConnectParams->TryGetStringField(TEXT("target_node_unique_name"), TargetNodeUniqueName) ||
            !ConnectParams->TryGetStringField(TEXT("source_pin"), SourcePin) ||
            !ConnectParams->TryGetStringField(TEXT("target_pin"), TargetPin))
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TEXT("Missing source/target node or pin fields"));
            FailedConnections.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        ConnectParams->SetStringField(TEXT("source_node_id"), SourceNodeUniqueName);
        ConnectParams->SetStringField(TEXT("target_node_id"), TargetNodeUniqueName);
        ConnectParams->SetStringField(TEXT("source_pin_name"), SourcePin);
        ConnectParams->SetStringField(TEXT("target_pin_name"), TargetPin);

        TSharedPtr<FJsonObject> ConnectResult = HandleConnectNodes(ConnectParams);
        const bool bSuccess = ConnectResult.IsValid() && (!ConnectResult->HasField(TEXT("success")) || ConnectResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), ConnectResult.IsValid() && ConnectResult->HasField(TEXT("error")) ? ConnectResult->GetStringField(TEXT("error")) : TEXT("Failed to connect nodes"));
            FailedConnections.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        TSharedPtr<FJsonObject> SuccessEntry = MakeShared<FJsonObject>();
        SuccessEntry->SetStringField(TEXT("source_node_unique_name"), SourceNodeUniqueName);
        SuccessEntry->SetStringField(TEXT("target_node_unique_name"), TargetNodeUniqueName);
        SuccessEntry->SetStringField(TEXT("source_pin"), SourcePin);
        SuccessEntry->SetStringField(TEXT("target_pin"), TargetPin);
        SuccessfulConnections.Add(MakeShared<FJsonValueObject>(SuccessEntry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), FailedConnections.Num() == 0);
    Result->SetNumberField(TEXT("total_connections"), Connections->Num());
    Result->SetNumberField(TEXT("successful_connections"), SuccessfulConnections.Num());
    Result->SetNumberField(TEXT("failed_connections"), FailedConnections.Num());
    Result->SetArrayField(TEXT("successful"), SuccessfulConnections);
    Result->SetArrayField(TEXT("failed"), FailedConnections);

    if (bCompileBlueprint && SuccessfulConnections.Num() > 0)
    {
        Result->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintRef));
    }

    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDisconnectBlueprintNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = LoadBlueprintForAuraGraph_BPGC(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing 'soft_path_from_project_root' parameter") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FString StrandName;
    FString GraphType;
    Params->TryGetStringField(TEXT("strand_name"), StrandName);
    Params->TryGetStringField(TEXT("graph_type"), GraphType);

    UEdGraph* Graph = ResolveGraphForAuraStrand_BPGC(Blueprint, StrandName, GraphType);
    if (!Graph)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph not found: %s"), *StrandName));
    }

    const TArray<TSharedPtr<FJsonValue>>* Disconnections = nullptr;
    if (!Params->TryGetArrayField(TEXT("disconnections"), Disconnections))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'disconnections' parameter"));
    }

    const bool bCompileBlueprint = ShouldCompileBlueprint_BPGC(Params);
    TArray<TSharedPtr<FJsonValue>> SuccessfulDisconnections;
    TArray<TSharedPtr<FJsonValue>> FailedDisconnections;

    for (int32 Index = 0; Index < Disconnections->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& DisconnectionValue = (*Disconnections)[Index];
        if (!DisconnectionValue.IsValid() || DisconnectionValue->Type != EJson::Object)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TEXT("Disconnection entry must be an object"));
            FailedDisconnections.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        const TSharedPtr<FJsonObject> DisconnectionObject = DisconnectionValue->AsObject();
        FString SourceNodeUniqueName;
        FString TargetNodeUniqueName;
        FString SourcePinName;
        FString TargetPinName;
        if (!DisconnectionObject->TryGetStringField(TEXT("source_node_unique_name"), SourceNodeUniqueName) ||
            !DisconnectionObject->TryGetStringField(TEXT("target_node_unique_name"), TargetNodeUniqueName) ||
            !DisconnectionObject->TryGetStringField(TEXT("source_pin"), SourcePinName) ||
            !DisconnectionObject->TryGetStringField(TEXT("target_pin"), TargetPinName))
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TEXT("Missing source/target node or pin fields"));
            FailedDisconnections.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        UEdGraphNode* SourceNode = FindNodeByUniqueName_BPGC(Graph, SourceNodeUniqueName);
        UEdGraphNode* TargetNode = FindNodeByUniqueName_BPGC(Graph, TargetNodeUniqueName);
        UEdGraphPin* SourcePin = FindPinByLooseName_BPGC(SourceNode, SourcePinName, EGPD_Output);
        UEdGraphPin* TargetPin = FindPinByLooseName_BPGC(TargetNode, TargetPinName, EGPD_Input);

        if (!SourceNode || !TargetNode || !SourcePin || !TargetPin)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TEXT("Node or pin not found"));
            FailedDisconnections.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        if (!SourcePin->LinkedTo.Contains(TargetPin))
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), Index);
            Failure->SetStringField(TEXT("error"), TEXT("Pins are not connected"));
            FailedDisconnections.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        SourceNode->Modify();
        TargetNode->Modify();
        SourcePin->Modify();
        TargetPin->Modify();
        SourcePin->BreakLinkTo(TargetPin);

        TSharedPtr<FJsonObject> SuccessEntry = MakeShared<FJsonObject>();
        SuccessEntry->SetStringField(TEXT("source_node_unique_name"), SourceNodeUniqueName);
        SuccessEntry->SetStringField(TEXT("target_node_unique_name"), TargetNodeUniqueName);
        SuccessEntry->SetStringField(TEXT("source_pin"), SourcePinName);
        SuccessEntry->SetStringField(TEXT("target_pin"), TargetPinName);
        SuccessfulDisconnections.Add(MakeShared<FJsonValueObject>(SuccessEntry));
    }

    if (SuccessfulDisconnections.Num() > 0)
    {
        Graph->NotifyGraphChanged();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), FailedDisconnections.Num() == 0);
    Result->SetNumberField(TEXT("total_disconnections"), Disconnections->Num());
    Result->SetNumberField(TEXT("successful_disconnections"), SuccessfulDisconnections.Num());
    Result->SetNumberField(TEXT("failed_disconnections"), FailedDisconnections.Num());
    Result->SetArrayField(TEXT("successful"), SuccessfulDisconnections);
    Result->SetArrayField(TEXT("failed"), FailedDisconnections);

    if (bCompileBlueprint && SuccessfulDisconnections.Num() > 0)
    {
        Result->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintRef));
    }

    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleRemoveBlueprintNodes(const TSharedPtr<FJsonObject>& Params)
{
    const FString BlueprintRef = ResolveBlueprintReference_BPGC(Params);
    if (BlueprintRef.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path_from_project_root' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* NodeNames = nullptr;
    if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("node_unique_names"), NodeNames))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_unique_names' parameter"));
    }

    const bool bCompileBlueprint = ShouldCompileBlueprint_BPGC(Params);
    TArray<TSharedPtr<FJsonValue>> RemovedNodeNames;
    TArray<TSharedPtr<FJsonValue>> NotFoundNodeNames;

    for (const TSharedPtr<FJsonValue>& NodeNameValue : *NodeNames)
    {
        FString NodeUniqueName;
        if (!NodeNameValue.IsValid() || !NodeNameValue->TryGetString(NodeUniqueName))
        {
            NotFoundNodeNames.Add(MakeShared<FJsonValueString>(TEXT("<invalid>")));
            continue;
        }

        TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
        ApplyBlueprintAndStrand_BPGC(Params, DeleteParams);
        DeleteParams->SetStringField(TEXT("node_id"), NodeUniqueName);

        TSharedPtr<FJsonObject> DeleteResult = HandleDeleteNode(DeleteParams);
        const bool bSuccess = DeleteResult.IsValid() && (!DeleteResult->HasField(TEXT("success")) || DeleteResult->GetBoolField(TEXT("success")));
        if (!bSuccess)
        {
            NotFoundNodeNames.Add(MakeShared<FJsonValueString>(NodeUniqueName));
            continue;
        }

        RemovedNodeNames.Add(MakeShared<FJsonValueString>(NodeUniqueName));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), NotFoundNodeNames.Num() == 0);
    Result->SetNumberField(TEXT("requested_count"), NodeNames->Num());
    Result->SetNumberField(TEXT("removed_count"), RemovedNodeNames.Num());
    Result->SetArrayField(TEXT("removed_node_unique_names"), RemovedNodeNames);
    Result->SetArrayField(TEXT("not_found_node_unique_names"), NotFoundNodeNames);

    if (bCompileBlueprint && RemovedNodeNames.Num() > 0)
    {
        Result->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintRef));
    }

    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodePinsDefaults(const TSharedPtr<FJsonObject>& Params)
{
    const FString BlueprintRef = ResolveBlueprintReference_BPGC(Params);
    if (BlueprintRef.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path_from_project_root' parameter"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("nodes"), Nodes))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'nodes' parameter"));
    }

    const bool bCompileBlueprint = ShouldCompileBlueprint_BPGC(Params);
    int32 TotalPinsProcessed = 0;
    int32 TotalPinsSuccessful = 0;
    int32 TotalPinsFailed = 0;
    TArray<TSharedPtr<FJsonValue>> SuccessfulNodes;
    TArray<TSharedPtr<FJsonValue>> FailedNodes;

    for (int32 NodeIndex = 0; NodeIndex < Nodes->Num(); ++NodeIndex)
    {
        const TSharedPtr<FJsonValue>& NodeValue = (*Nodes)[NodeIndex];
        if (!NodeValue.IsValid() || NodeValue->Type != EJson::Object)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), NodeIndex);
            Failure->SetStringField(TEXT("error"), TEXT("Node entry must be an object"));
            FailedNodes.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
        FString NodeUniqueName;
        if (!NodeObject->TryGetStringField(TEXT("node_unique_name"), NodeUniqueName))
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), NodeIndex);
            Failure->SetStringField(TEXT("error"), TEXT("Missing 'node_unique_name' parameter"));
            FailedNodes.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        const TSharedPtr<FJsonObject>* PinDefaultsObject = nullptr;
        if (!NodeObject->TryGetObjectField(TEXT("pin_defaults"), PinDefaultsObject) || !PinDefaultsObject || !PinDefaultsObject->IsValid())
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetNumberField(TEXT("index"), NodeIndex);
            Failure->SetStringField(TEXT("error"), TEXT("Missing 'pin_defaults' parameter"));
            FailedNodes.Add(MakeShared<FJsonValueObject>(Failure));
            continue;
        }

        TArray<TSharedPtr<FJsonValue>> AppliedPins;
        TArray<TSharedPtr<FJsonValue>> FailedPins;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& PinEntry : (*PinDefaultsObject)->Values)
        {
            ++TotalPinsProcessed;

            TSharedPtr<FJsonObject> SetParams = MakeShared<FJsonObject>();
            ApplyBlueprintAndStrand_BPGC(Params, SetParams);
            SetParams->SetStringField(TEXT("node_id"), NodeUniqueName);
            SetParams->SetStringField(TEXT("action"), TEXT("set_pin_default"));
            SetParams->SetStringField(TEXT("pin_name"), PinEntry.Key);

            if (PinEntry.Value.IsValid() && PinEntry.Value->Type == EJson::String)
            {
                const FString RawString = PinEntry.Value->AsString();
                const FString ResolvedObjectPath = ResolveDefaultObjectPath_BPGC(PinEntry.Key, RawString);
                if (ResolvedObjectPath.StartsWith(TEXT("/")))
                {
                    SetParams->SetStringField(TEXT("default_object_path"), ResolvedObjectPath);
                    SetParams->SetStringField(TEXT("value_kind"), PinEntry.Key.EndsWith(TEXT("Class"), ESearchCase::IgnoreCase) ? TEXT("class") : TEXT("object"));
                }
                else
                {
                    SetParams->SetStringField(TEXT("default_value"), RawString);
                }
            }
            else
            {
                SetParams->SetStringField(TEXT("default_value"), JsonValueToDefaultString_BPGC(PinEntry.Key, PinEntry.Value));
            }

            TSharedPtr<FJsonObject> SetResult = HandleSetNodeProperty(SetParams);
            const bool bPinSuccess = SetResult.IsValid() && (!SetResult->HasField(TEXT("success")) || SetResult->GetBoolField(TEXT("success")));
            if (!bPinSuccess)
            {
                ++TotalPinsFailed;
                TSharedPtr<FJsonObject> FailedPin = MakeShared<FJsonObject>();
                FailedPin->SetStringField(TEXT("pin_name"), PinEntry.Key);
                FailedPin->SetStringField(TEXT("error"), SetResult.IsValid() && SetResult->HasField(TEXT("error")) ? SetResult->GetStringField(TEXT("error")) : TEXT("Failed to set pin default"));
                FailedPins.Add(MakeShared<FJsonValueObject>(FailedPin));
                continue;
            }

            ++TotalPinsSuccessful;
            TSharedPtr<FJsonObject> AppliedPin = MakeShared<FJsonObject>();
            AppliedPin->SetStringField(TEXT("pin_name"), PinEntry.Key);
            AppliedPin->SetStringField(TEXT("default_as_string"), SetResult->HasField(TEXT("default_as_string")) ? SetResult->GetStringField(TEXT("default_as_string")) : FString());
            AppliedPins.Add(MakeShared<FJsonValueObject>(AppliedPin));
        }

        if (FailedPins.Num() > 0)
        {
            TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
            Failure->SetStringField(TEXT("node_unique_name"), NodeUniqueName);
            Failure->SetArrayField(TEXT("failed_pins"), FailedPins);
            Failure->SetNumberField(TEXT("failed_pin_count"), FailedPins.Num());
            FailedNodes.Add(MakeShared<FJsonValueObject>(Failure));
        }
        else
        {
            TSharedPtr<FJsonObject> SuccessEntry = MakeShared<FJsonObject>();
            SuccessEntry->SetStringField(TEXT("node_unique_name"), NodeUniqueName);
            SuccessEntry->SetArrayField(TEXT("applied_pins"), AppliedPins);
            SuccessEntry->SetNumberField(TEXT("applied_pin_count"), AppliedPins.Num());
            SuccessfulNodes.Add(MakeShared<FJsonValueObject>(SuccessEntry));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), FailedNodes.Num() == 0);
    Result->SetNumberField(TEXT("total_nodes"), Nodes->Num());
    Result->SetNumberField(TEXT("successful_nodes"), SuccessfulNodes.Num());
    Result->SetNumberField(TEXT("failed_nodes"), FailedNodes.Num());
    Result->SetNumberField(TEXT("total_pins_processed"), TotalPinsProcessed);
    Result->SetNumberField(TEXT("total_pins_successful"), TotalPinsSuccessful);
    Result->SetNumberField(TEXT("total_pins_failed"), TotalPinsFailed);
    Result->SetArrayField(TEXT("successful"), SuccessfulNodes);
    Result->SetArrayField(TEXT("failed"), FailedNodes);

    if (bCompileBlueprint && TotalPinsSuccessful > 0)
    {
        Result->SetObjectField(TEXT("compile_result"), CompileBlueprint_BPGC(BlueprintRef));
    }

    return Result;
}


TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCreateFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleCreateFunction: Creating function '%s' in blueprint '%s'"),
        *FunctionName, *BlueprintName);

    return FFunctionManager::CreateFunction(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionInput(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionInput: Adding input '%s' to function '%s' in blueprint '%s'"),
        *ParamName, *FunctionName, *BlueprintName);

    return FFunctionIO::AddFunctionInput(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionOutput(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionOutput: Adding output '%s' to function '%s' in blueprint '%s'"),
        *ParamName, *FunctionName, *BlueprintName);

    return FFunctionIO::AddFunctionOutput(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteFunction: Deleting function '%s' from blueprint '%s'"),
        *FunctionName, *BlueprintName);

    return FFunctionManager::DeleteFunction(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleRenameFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString OldFunctionName;
    if (!Params->TryGetStringField(TEXT("old_function_name"), OldFunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'old_function_name' parameter"));
    }

    FString NewFunctionName;
    if (!Params->TryGetStringField(TEXT("new_function_name"), NewFunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleRenameFunction: Renaming function '%s' to '%s' in blueprint '%s'"),
        *OldFunctionName, *NewFunctionName, *BlueprintName);

    return FFunctionManager::RenameFunction(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleFormatGraph(const TSharedPtr<FJsonObject>& Params)
{
    return FGraphFormatter::FormatGraph(Params);
}
