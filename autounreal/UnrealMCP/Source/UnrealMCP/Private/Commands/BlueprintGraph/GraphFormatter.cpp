#include "Commands/BlueprintGraph/GraphFormatter.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "Algo/Sort.h"
#include "Misc/Paths.h"

namespace
{
    bool IsExecPin(const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
    }

    bool HasExecPins(const UEdGraphNode* Node)
    {
        if (!Node)
        {
            return false;
        }
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (IsExecPin(Pin))
            {
                return true;
            }
        }
        return false;
    }
}

TSharedPtr<FJsonObject> FGraphFormatter::FormatGraph(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid parameters"));
    }

    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString GraphName;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);
    if (GraphName.IsEmpty())
    {
        Params->TryGetStringField(TEXT("function_name"), GraphName);
    }

    double SpacingX = 400.0;
    double SpacingY = 160.0;
    double StartX = 0.0;
    double StartY = 0.0;
    Params->TryGetNumberField(TEXT("spacing_x"), SpacingX);
    Params->TryGetNumberField(TEXT("spacing_y"), SpacingY);
    Params->TryGetNumberField(TEXT("start_x"), StartX);
    Params->TryGetNumberField(TEXT("start_y"), StartY);

    FString Direction = TEXT("left_to_right");
    Params->TryGetStringField(TEXT("direction"), Direction);
    const bool bTopToBottom = Direction.Equals(TEXT("top_to_bottom"), ESearchCase::IgnoreCase);

    UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UEdGraph* Graph = GetTargetGraph(Blueprint, GraphName);
    if (!Graph)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Graph not found"));
    }

    TArray<UEdGraphNode*> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }
        const FString ClassName = Node->GetClass()->GetName();
        if (ClassName.Contains(TEXT("Comment")))
        {
            continue;
        }
        Nodes.Add(Node);
    }

    if (Nodes.Num() == 0)
    {
        TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("success"), true);
        Response->SetNumberField(TEXT("node_count"), 0);
        Response->SetStringField(TEXT("graph_name"), Graph->GetName());
        return Response;
    }

    TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Adjacency;
    TMap<UEdGraphNode*, int32> InDegree;
    TMap<UEdGraphNode*, int32> Depth;

    for (UEdGraphNode* Node : Nodes)
    {
        InDegree.Add(Node, 0);
        Depth.Add(Node, -1);
    }

    for (UEdGraphNode* Node : Nodes)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output || !IsExecPin(Pin))
            {
                continue;
            }
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (!Linked)
                {
                    continue;
                }
                UEdGraphNode* TargetNode = Linked->GetOwningNode();
                if (!TargetNode || TargetNode == Node)
                {
                    continue;
                }
                if (!InDegree.Contains(TargetNode))
                {
                    continue;
                }
                Adjacency.FindOrAdd(Node).AddUnique(TargetNode);
                InDegree[TargetNode] = InDegree[TargetNode] + 1;
            }
        }
    }

    TArray<UEdGraphNode*> Roots;
    for (UEdGraphNode* Node : Nodes)
    {
        const bool bIsEvent = Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_FunctionEntry>();
        if (bIsEvent || (HasExecPins(Node) && InDegree.FindRef(Node) == 0))
        {
            Roots.Add(Node);
        }
    }

    if (Roots.Num() == 0 && Nodes.Num() > 0)
    {
        Roots.Add(Nodes[0]);
    }

    TArray<UEdGraphNode*> Queue;
    for (UEdGraphNode* Root : Roots)
    {
        Depth[Root] = 0;
        Queue.Add(Root);
    }

    for (int32 i = 0; i < Queue.Num(); ++i)
    {
        UEdGraphNode* Current = Queue[i];
        const int32 CurrentDepth = Depth.FindRef(Current);
        const TArray<UEdGraphNode*>* Neighbors = Adjacency.Find(Current);
        if (!Neighbors)
        {
            continue;
        }
        for (UEdGraphNode* Next : *Neighbors)
        {
            int32& NextDepth = Depth.FindChecked(Next);
            const int32 Proposed = CurrentDepth + 1;
            if (NextDepth < Proposed)
            {
                NextDepth = Proposed;
                Queue.Add(Next);
            }
        }
    }

    int32 MaxDepth = 0;
    for (auto& Pair : Depth)
    {
        MaxDepth = FMath::Max(MaxDepth, Pair.Value);
    }

    for (UEdGraphNode* Node : Nodes)
    {
        if (Depth.FindRef(Node) != -1)
        {
            continue;
        }

        int32 BestDepth = MAX_int32;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (!Linked)
                {
                    continue;
                }
                UEdGraphNode* LinkedNode = Linked->GetOwningNode();
                if (!LinkedNode)
                {
                    continue;
                }
                if (!Depth.Contains(LinkedNode) || Depth[LinkedNode] < 0)
                {
                    continue;
                }
                BestDepth = FMath::Min(BestDepth, Depth[LinkedNode]);
            }
        }

        if (BestDepth != MAX_int32)
        {
            Depth[Node] = FMath::Max(0, BestDepth - 1);
        }
        else
        {
            Depth[Node] = MaxDepth + 1;
        }
    }

    TMap<int32, TArray<UEdGraphNode*>> Columns;
    for (UEdGraphNode* Node : Nodes)
    {
        Columns.FindOrAdd(Depth.FindRef(Node)).Add(Node);
    }

    TArray<int32> ColumnKeys;
    Columns.GetKeys(ColumnKeys);
    ColumnKeys.Sort();

    int32 Repositioned = 0;
    for (int32 DepthKey : ColumnKeys)
    {
        TArray<UEdGraphNode*>& ColumnNodes = Columns[DepthKey];
        Algo::Sort(ColumnNodes, [](const UEdGraphNode* A, const UEdGraphNode* B)
        {
            return A->NodePosY < B->NodePosY;
        });

        for (int32 Index = 0; Index < ColumnNodes.Num(); ++Index)
        {
            UEdGraphNode* Node = ColumnNodes[Index];
            const int32 X = static_cast<int32>(StartX + (bTopToBottom ? Index : DepthKey) * SpacingX);
            const int32 Y = static_cast<int32>(StartY + (bTopToBottom ? DepthKey : Index) * SpacingY);
            Node->NodePosX = X;
            Node->NodePosY = Y;
            Repositioned++;
        }
    }

    Graph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetBoolField(TEXT("success"), true);
    Response->SetNumberField(TEXT("node_count"), Repositioned);
    Response->SetStringField(TEXT("graph_name"), Graph->GetName());
    return Response;
}

UBlueprint* FGraphFormatter::LoadBlueprint(const FString& BlueprintName)
{
    FString BlueprintPath = BlueprintName;

    if (!BlueprintPath.StartsWith(TEXT("/")))
    {
        BlueprintPath = TEXT("/Game/Blueprints/") + BlueprintPath;
    }

    if (!BlueprintPath.Contains(TEXT(".")))
    {
        BlueprintPath += TEXT(".") + FPaths::GetBaseFilename(BlueprintPath);
    }

    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
    if (!BP)
    {
        if (UEditorAssetLibrary::DoesAssetExist(BlueprintPath))
        {
            UObject* Asset = UEditorAssetLibrary::LoadAsset(BlueprintPath);
            BP = Cast<UBlueprint>(Asset);
        }
    }

    return BP;
}

UEdGraph* FGraphFormatter::GetTargetGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    if (!Blueprint)
    {
        return nullptr;
    }

    if (GraphName.IsEmpty())
    {
        if (Blueprint->UbergraphPages.Num() > 0)
        {
            return Blueprint->UbergraphPages[0];
        }
        return nullptr;
    }

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph && (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase) || Graph->GetName().Contains(GraphName)))
        {
            return Graph;
        }
    }

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph && (Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase) || Graph->GetName().Contains(GraphName)))
        {
            return Graph;
        }
    }

    return nullptr;
}
