#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Editor-related MCP commands
 * Handles viewport control, actor manipulation, and level management
 */
class UNREALMCP_API FEpicUnrealMCPEditorCommands
{
public:
    	FEpicUnrealMCPEditorCommands();

    // Handle editor commands
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    // Actor manipulation commands
    TSharedPtr<FJsonObject> HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateInputActionKey(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateInputActions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddInputActionToMappingContext(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateGameplayTag(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddOrReplaceRowsInDataTable(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveRowsFromDataTable(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetDataAssetTypes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetDataAssetTypeInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleEditEnumeration(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleEditStructure(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateTextFile(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleEditTextFile(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateNewTodoList(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetTodoList(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleEditTodoList(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSearchAssetsByNameFromFab(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetsByNameFromFab(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddFabAssetToProject(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSaveProject(const TSharedPtr<FJsonObject>& Params);

    // Blueprint actor spawning
    TSharedPtr<FJsonObject> HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params);

    // Pose Search Database
    TSharedPtr<FJsonObject> HandleAddAnimationsToPoseSearchDatabase(const TSharedPtr<FJsonObject>& Params);
}; 
