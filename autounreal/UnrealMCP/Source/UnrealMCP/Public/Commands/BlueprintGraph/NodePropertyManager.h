#pragma once

#include "CoreMinimal.h"
#include "Json.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

class UEdGraph;
class UEdGraphNode;
class UBlueprint;
class UK2Node;

/**
 * Manages Blueprint node property modification
 * Supports modifying properties on various node types
 */
class UNREALMCP_API FNodePropertyManager
{
public:
	/**
	 * Set a property on a Blueprint node
	 * @param Params JSON parameters containing:
	 *   - blueprint_name (string): Name of the Blueprint
	 *   - node_id (string): ID of the node
	 *   - property_name (string): Name of property to set
	 *   - property_value (any): Value to set
	 *   - function_name (string, optional): Function graph name (null = EventGraph)
	 * @return JSON response with updated_property or error
	 */
	static TSharedPtr<FJsonObject> SetNodeProperty(const TSharedPtr<FJsonObject>& Params);

	/**
	 * Edit a Blueprint node with semantic actions (add pins, change types, etc.)
	 * @param Params JSON parameters containing:
	 *   - blueprint_name (string): Name of the Blueprint
	 *   - node_id (string): ID of the node
	 *   - action (string): Action to perform (set_enum_type, add_pin, remove_pin, set_num_elements,
	 *     split_pin, break_pin_links, disconnect_pin, set_cast_target, set_function_call, set_pin_default)
	 *   - function_name (string, optional): Function graph name (null = EventGraph)
	 *   - Additional parameters depend on action
	 * @return JSON response with success and action-specific results
	 */
	static TSharedPtr<FJsonObject> EditNode(const TSharedPtr<FJsonObject>& Params);

	/**
	 * Extract pin default specs from params.
	 * Supports:
	 *   - pin_defaults: [ {...}, {...} ]
	 *   - pin_defaults: { ... }
	 *   - pin_default: { ... }
	 */
	static bool ExtractPinDefaultSpecs(const TSharedPtr<FJsonObject>& Params, TArray<TSharedPtr<FJsonValue>>& OutPinDefaults);

	/**
	 * Apply a typed default value spec to a single pin on a K2 node.
	 * Required fields:
	 *   - pin_name
	 * Optional fields:
	 *   - value_kind
	 *   - default_value
	 *   - default_object_path / default_object
	 *   - default_text / default_text_value
	 *   - clear_default
	 */
	static bool ApplyPinDefaultSpec(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		UK2Node* Node,
		const TSharedPtr<FJsonObject>& Params,
		TSharedPtr<FJsonObject>& OutAppliedResult,
		FString& OutErrorMessage);

	/**
	 * Apply multiple pin default specs and notify the graph once on success.
	 */
	static bool ApplyPinDefaults(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		UK2Node* Node,
		const TArray<TSharedPtr<FJsonValue>>& PinDefaults,
		TArray<TSharedPtr<FJsonValue>>& OutAppliedResults,
		FString& OutErrorMessage);

private:
	/**
	 * Get the appropriate graph (EventGraph or Function Graph)
	 */
	static UEdGraph* GetGraph(UBlueprint* Blueprint, const FString& FunctionName);

	/**
	 * Find a node by its ID
	 */
	static UEdGraphNode* FindNodeByID(UEdGraph* Graph, const FString& NodeID);

	/**
	 * Set property on a Print (CallFunction) node
	 */
	static bool SetPrintNodeProperty(
		UK2Node_CallFunction* PrintNode,
		const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value);

	/**
	 * Set property on a Variable Get/Set node
	 */
	static bool SetVariableNodeProperty(
		UK2Node* VarNode,
		const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value);

	/**
	 * Set generic property (position, comment, etc.)
	 */
	static bool SetGenericNodeProperty(
		UEdGraphNode* Node,
		const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value);

	/**
	 * Load a Blueprint by name
	 */
	static UBlueprint* LoadBlueprint(const FString& BlueprintName);

	// Routing dispatcher for edit actions
	// Delegates to specialized editors (PinManagementEditor, TypeModificationEditor, ReferenceUpdateEditor)
	static TSharedPtr<FJsonObject> DispatchEditAction(
		UK2Node* Node,
		UEdGraph* Graph,
		const FString& Action,
		const TSharedPtr<FJsonObject>& Params);

	// Helper functions
	static TSharedPtr<FJsonObject> CreateSuccessResponse(const FString& PropertyName);
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage);
	static TSharedPtr<FJsonObject> CreateActionResponse(
		bool bSuccess,
		const FString& Action,
		const TSharedPtr<FJsonObject>& Details = nullptr);
};
