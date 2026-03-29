// Header for creating data nodes (Variable Get/Set, arrays, struct operations)

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"

class UK2Node;

/**
 * Creator for Unreal Blueprint data management nodes
 */
class FDataNodeCreator
{
public:
	/**
	 * Creates a Variable Get node (K2Node_VariableGet)
	 * @param Graph - The graph to add the node to
	 * @param Params - JSON parameters containing pos_x, pos_y, variable_name
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateVariableGetNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Creates a Variable Set node (K2Node_VariableSet)
	 * @param Graph - The graph to add the node to
	 * @param Params - JSON parameters containing pos_x, pos_y, variable_name
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateVariableSetNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Creates a Make Array node (K2Node_MakeArray)
	 * @param Graph - The graph to add the node to
	 * @param Params - JSON parameters containing pos_x, pos_y, element_type
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateMakeArrayNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Creates a Make Struct node (K2Node_MakeStruct)
	 * @param Graph - The graph to add the node to
	 * @param Params - Node parameters containing pos_x, pos_y, struct_type
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateMakeStructNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Creates a Break Struct node (K2Node_BreakStruct)
	 * @param Graph - The graph to add the node to
	 * @param Params - Node parameters containing pos_x, pos_y, struct_type
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateBreakStructNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Creates a Set Fields In Struct node (K2Node_SetFieldsInStruct)
	 * @param Graph - The graph to add the node to
	 * @param Params - Node parameters containing pos_x, pos_y, struct_type
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateSetFieldsInStructNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
};
