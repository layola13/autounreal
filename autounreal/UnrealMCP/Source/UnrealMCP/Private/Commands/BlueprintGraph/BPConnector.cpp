#include "Commands/BlueprintGraph/BPConnector.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"

namespace
{
    FString NormalizeLoosePinName_BPC(const FString& InName)
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

    bool DoesPinNameMatchLoose_BPC(const UEdGraphPin* Pin, const FString& DesiredName)
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

        if (NormalizeLoosePinName_BPC(PinName).Equals(NormalizeLoosePinName_BPC(DesiredName), ESearchCase::IgnoreCase))
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

    UEdGraphPin* FindPinRecursiveByLooseName_BPC(
        const TArray<UEdGraphPin*>& Pins,
        const FString& PinName,
        const EEdGraphPinDirection Direction)
    {
        for (UEdGraphPin* Pin : Pins)
        {
            if (!Pin)
            {
                continue;
            }

            if (Pin->Direction == Direction && DoesPinNameMatchLoose_BPC(Pin, PinName))
            {
                return Pin;
            }

            if (Pin->SubPins.Num() > 0)
            {
                if (UEdGraphPin* SubPin = FindPinRecursiveByLooseName_BPC(Pin->SubPins, PinName, Direction))
                {
                    return SubPin;
                }
            }
        }

        return nullptr;
    }
}

TSharedPtr<FJsonObject> FBPConnector::ConnectNodes(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    // Extraire paramètres
    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString SourceNodeId = Params->GetStringField(TEXT("source_node_id"));
    FString SourcePinName = Params->GetStringField(TEXT("source_pin_name"));
    FString TargetNodeId = Params->GetStringField(TEXT("target_node_id"));
    FString TargetPinName = Params->GetStringField(TEXT("target_pin_name"));

    FString FunctionName;
    Params->TryGetStringField(TEXT("function_name"), FunctionName);

    // Charger Blueprint - handle both full paths and simple names
    UBlueprint* Blueprint = nullptr;
    FString BlueprintPath = BlueprintName;

    // If no path prefix, assume /Game/Blueprints/
    if (!BlueprintPath.StartsWith(TEXT("/")))
    {
        BlueprintPath = TEXT("/Game/Blueprints/") + BlueprintPath;
    }

    // Add .Blueprint suffix if not present
    if (!BlueprintPath.Contains(TEXT(".")))
    {
        BlueprintPath += TEXT(".") + FPaths::GetBaseFilename(BlueprintPath);
    }

    // Try to load the Blueprint
    Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

    // If not found, try with UEditorAssetLibrary
    if (!Blueprint)
    {
        FString AssetPath = BlueprintPath;
        if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
        {
            UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
            Blueprint = Cast<UBlueprint>(Asset);
        }
    }

    if (!Blueprint)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Blueprint not found");
        return Result;
    }

    // Get graph
    UEdGraph* Graph = nullptr;

    if (!FunctionName.IsEmpty())
    {
        // Strategy 1: Try exact name match with GetFName()
        for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
        {
            if (FuncGraph && (FuncGraph->GetFName().ToString() == FunctionName ||
                              (FuncGraph->GetOuter() && FuncGraph->GetOuter()->GetFName().ToString() == FunctionName)))
            {
                Graph = FuncGraph;
                break;
            }
        }

        // Strategy 2: Fallback - partial match for auto-generated names
        if (!Graph)
        {
            for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
            {
                if (FuncGraph && FuncGraph->GetFName().ToString().Contains(FunctionName))
                {
                    Graph = FuncGraph;
                    break;
                }
            }
        }

        if (!Graph)
        {
            Result->SetBoolField("success", false);
            Result->SetStringField("error", FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
            return Result;
        }
    }
    else
    {
        // Use event graph if no function specified
        if (Blueprint->UbergraphPages.Num() == 0)
        {
            Result->SetBoolField("success", false);
            Result->SetStringField("error", "Blueprint has no event graph");
            return Result;
        }

        Graph = Blueprint->UbergraphPages[0];
    }

    if (!Graph)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Graph not found");
        return Result;
    }

    // Find nodes
    UEdGraphNode* SourceNode = FindNodeById(Graph, SourceNodeId);
    UEdGraphNode* TargetNode = FindNodeById(Graph, TargetNodeId);

    if (!SourceNode || !TargetNode)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Node not found");
        return Result;
    }

    // Trouver pins
    UEdGraphPin* SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Output);
    UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Input);

    if (!SourcePin || !TargetPin)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Pin not found");
        return Result;
    }

    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Graph schema not found");
        return Result;
    }

    // Validate compatibility and create connection through the schema so split pins/wildcards are handled correctly
    if (!ArePinsCompatible(SourcePin, TargetPin))
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Pins not compatible");
        return Result;
    }

    if (!Schema->TryCreateConnection(SourcePin, TargetPin))
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Failed to create connection");
        return Result;
    }

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    Blueprint->MarkPackageDirty();
    if (bCompileBlueprint)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }

    // Return
    Result->SetBoolField("success", true);

    TSharedPtr<FJsonObject> ConnectionInfo = MakeShared<FJsonObject>();
    ConnectionInfo->SetStringField("source_node", SourceNodeId);
    ConnectionInfo->SetStringField("source_pin", SourcePinName);
    ConnectionInfo->SetStringField("target_node", TargetNodeId);
    ConnectionInfo->SetStringField("target_pin", TargetPinName);
    ConnectionInfo->SetStringField("connection_type", SourcePin->PinType.PinCategory.ToString());
    ConnectionInfo->SetBoolField("compiled", bCompileBlueprint);

    Result->SetObjectField("connection", ConnectionInfo);

    return Result;
}

UEdGraphNode* FBPConnector::FindNodeById(UEdGraph* Graph, const FString& NodeId)
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

        // Try matching by NodeGuid first
        if (Node->NodeGuid.ToString().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            return Node;
        }

        // Try matching by GetName()
        if (Node->GetName().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }

    return nullptr;
}

UEdGraphPin* FBPConnector::FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
    if (!Node)
    {
        return nullptr;
    }

    return FindPinRecursiveByLooseName_BPC(Node->Pins, PinName, Direction);
}

bool FBPConnector::ArePinsCompatible(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
{
    if (!SourcePin || !TargetPin)
    {
        return false;
    }

    if (SourcePin->Direction != EGPD_Output || TargetPin->Direction != EGPD_Input)
    {
        return false;
    }

    if (const UEdGraphSchema* Schema = SourcePin->GetSchema())
    {
        const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
        if (Response.Response != CONNECT_RESPONSE_DISALLOW)
        {
            return true;
        }
    }

    const FName& SourceCategory = SourcePin->PinType.PinCategory;
    const FName& TargetCategory = TargetPin->PinType.PinCategory;

    if (SourceCategory == TargetCategory)
    {
        if (SourceCategory == UEdGraphSchema_K2::PC_Object)
        {
            UClass* SourceClass = Cast<UClass>(SourcePin->PinType.PinSubCategoryObject.Get());
            UClass* TargetClass = Cast<UClass>(TargetPin->PinType.PinSubCategoryObject.Get());

            if (!SourceClass || !TargetClass)
            {
                return true;
            }

            return SourceClass->IsChildOf(TargetClass) || TargetClass->IsChildOf(SourceClass);
        }

        return true;
    }

    // Dynamic cast object pins start as wildcard until a compatible object is linked.
    if (SourceCategory == UEdGraphSchema_K2::PC_Wildcard || TargetCategory == UEdGraphSchema_K2::PC_Wildcard)
    {
        return true;
    }

    return false;
}
