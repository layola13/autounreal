#include "Commands/BlueprintGraph/Nodes/AnimGraphNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_MotionMatching.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "EditorAssetLibrary.h"
#include "EdGraph/EdGraph.h"
#include "UObject/UnrealType.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Misc/Paths.h"

static void ExtractAnimNodePosition(const TSharedPtr<FJsonObject>& Params, double& OutX, double& OutY)
{
	FNodeCreatorUtils::ExtractNodePosition(Params, OutX, OutY);
}

static void InitializeAnimGraphNode(UEdGraphNode* Node, UEdGraph* Graph)
{
	if (!Node || !Graph)
	{
		return;
	}

	Node->AllocateDefaultPins();
	Node->ReconstructNode();
	Graph->NotifyGraphChanged();
}

static void ApplyUpdateFunction(UAnimGraphNode_Base* Node, const FString& UpdateFunction)
{
	if (!Node || UpdateFunction.IsEmpty())
	{
		return;
	}

	Node->UpdateFunction.SetSelfMember(FName(*UpdateFunction));
}

static bool ApplyMotionMatchingDatabase(UAnimGraphNode_MotionMatching* Node, const FString& DatabasePath)
{
	if (!Node || DatabasePath.IsEmpty())
	{
		return false;
	}

	FString AssetPath = DatabasePath;
	if (!AssetPath.StartsWith(TEXT("/")))
	{
		AssetPath = TEXT("/Game/") + AssetPath;
	}
	if (!AssetPath.Contains(TEXT(".")))
	{
		const FString BaseName = FPaths::GetBaseFilename(AssetPath);
		AssetPath += TEXT(".") + BaseName;
	}

	UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Database)
	{
		Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath);
	}

	if (!Database)
	{
		return false;
	}

	FStructProperty* NodeProp = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("Node"));
	if (!NodeProp)
	{
		return false;
	}

	void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(Node);
	if (!NodePtr)
	{
		return false;
	}

	if (FObjectPropertyBase* DbProp = FindFProperty<FObjectPropertyBase>(NodeProp->Struct, TEXT("Database")))
	{
		DbProp->SetObjectPropertyValue_InContainer(NodePtr, Database);
		return true;
	}

	return false;
}

UEdGraphNode* FAnimGraphNodeCreator::CreateMotionMatchingNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UAnimGraphNode_MotionMatching* MotionNode = NewObject<UAnimGraphNode_MotionMatching>(Graph);
	if (!MotionNode)
	{
		return nullptr;
	}

	double PosX = 0.0;
	double PosY = 0.0;
	ExtractAnimNodePosition(Params, PosX, PosY);
	MotionNode->NodePosX = static_cast<int32>(PosX);
	MotionNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MotionNode, false, false);
	MotionNode->CreateNewGuid();
	MotionNode->PostPlacedNewNode();
	InitializeAnimGraphNode(MotionNode, Graph);

	FString UpdateFunction;
	if (!Params->TryGetStringField(TEXT("update_function"), UpdateFunction))
	{
		Params->TryGetStringField(TEXT("target_function"), UpdateFunction);
	}
	ApplyUpdateFunction(MotionNode, UpdateFunction);

	FString DatabasePath;
	if (!Params->TryGetStringField(TEXT("database_path"), DatabasePath))
	{
		Params->TryGetStringField(TEXT("target_blueprint"), DatabasePath);
	}
	ApplyMotionMatchingDatabase(MotionNode, DatabasePath);

	return MotionNode;
}

UEdGraphNode* FAnimGraphNodeCreator::CreatePoseSearchHistoryCollectorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UAnimGraphNode_PoseSearchHistoryCollector* HistoryNode = NewObject<UAnimGraphNode_PoseSearchHistoryCollector>(Graph);
	if (!HistoryNode)
	{
		return nullptr;
	}

	double PosX = 0.0;
	double PosY = 0.0;
	ExtractAnimNodePosition(Params, PosX, PosY);
	HistoryNode->NodePosX = static_cast<int32>(PosX);
	HistoryNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(HistoryNode, false, false);
	HistoryNode->CreateNewGuid();
	HistoryNode->PostPlacedNewNode();
	InitializeAnimGraphNode(HistoryNode, Graph);

	FString UpdateFunction;
	if (!Params->TryGetStringField(TEXT("update_function"), UpdateFunction))
	{
		Params->TryGetStringField(TEXT("target_function"), UpdateFunction);
	}
	ApplyUpdateFunction(HistoryNode, UpdateFunction);

	return HistoryNode;
}

UEdGraphNode* FAnimGraphNodeCreator::CreatePoseSearchComponentSpaceHistoryCollectorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UAnimGraphNode_PoseSearchComponentSpaceHistoryCollector* HistoryNode = NewObject<UAnimGraphNode_PoseSearchComponentSpaceHistoryCollector>(Graph);
	if (!HistoryNode)
	{
		return nullptr;
	}

	double PosX = 0.0;
	double PosY = 0.0;
	ExtractAnimNodePosition(Params, PosX, PosY);
	HistoryNode->NodePosX = static_cast<int32>(PosX);
	HistoryNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(HistoryNode, false, false);
	HistoryNode->CreateNewGuid();
	HistoryNode->PostPlacedNewNode();
	InitializeAnimGraphNode(HistoryNode, Graph);

	FString UpdateFunction;
	if (!Params->TryGetStringField(TEXT("update_function"), UpdateFunction))
	{
		Params->TryGetStringField(TEXT("target_function"), UpdateFunction);
	}
	ApplyUpdateFunction(HistoryNode, UpdateFunction);

	return HistoryNode;
}
