#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class FEpicUnrealMCPEditorCommands;
class FEpicUnrealMCPBlueprintCommands;

/**
 * Read-only inspector style commands that approximate Aura's inspector surface.
 */
class UNREALMCP_API FEpicUnrealMCPInspectorCommands
{
public:
    FEpicUnrealMCPInspectorCommands(
        const TSharedPtr<FEpicUnrealMCPEditorCommands>& InEditorCommands,
        const TSharedPtr<FEpicUnrealMCPBlueprintCommands>& InBlueprintCommands);

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleGetHeadlessStatus(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleLaunchUnrealProject(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleShutdownHeadless(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetUnrealContext(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleQueryUnrealProjectAssets(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleQuicksearch(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGrep(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetCodeExamples(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetMeta(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetStructs(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlueprintMaterialProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReviewBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateAndValidateBlueprintPlan(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetTextFileContents(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAvailableActorsInLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetEnums(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetGameplayTags(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadDatatableKeys(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadDatatableValues(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetRecentGeneratedImages(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFetchAnimationSkill(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateOrEditPlan(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFetchGasBestPractices(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFetchUiBestPractices(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleImportUnderstanding(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleInspectCurrentLevel(const TSharedPtr<FJsonObject>& Params);

    TSharedPtr<FEpicUnrealMCPEditorCommands> EditorCommands;
    TSharedPtr<FEpicUnrealMCPBlueprintCommands> BlueprintCommands;
};
