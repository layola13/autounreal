// Blueprint Graph Formatter
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FGraphFormatter
{
public:
    static TSharedPtr<FJsonObject> FormatGraph(const TSharedPtr<FJsonObject>& Params);

private:
    static UBlueprint* LoadBlueprint(const FString& BlueprintName);
    static UEdGraph* GetTargetGraph(UBlueprint* Blueprint, const FString& GraphName);
};
