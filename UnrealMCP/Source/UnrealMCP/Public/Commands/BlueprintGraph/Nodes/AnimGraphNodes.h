#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UEdGraph;
class UEdGraphNode;

class FAnimGraphNodeCreator
{
public:
	static UEdGraphNode* CreateMotionMatchingNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params);
	static UEdGraphNode* CreatePoseSearchHistoryCollectorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params);
	static UEdGraphNode* CreatePoseSearchComponentSpaceHistoryCollectorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params);
};
