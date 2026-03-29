#include "Commands/BlueprintGraph/Nodes/DataNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_SetFieldsInStruct.h"
#include "EditorAssetLibrary.h"
#include "Misc/Paths.h"
#include "Json.h"

namespace
{
	bool IsLikelyGeneratedStructPath(const FString& StructPath)
	{
		return StructPath.StartsWith(TEXT("/")) && !StructPath.Contains(TEXT("."));
	}

	UScriptStruct* ResolveStructType(const FString& StructTypeString)
	{
		if (StructTypeString.IsEmpty())
		{
			return nullptr;
		}

		if (UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*StructTypeString))
		{
			return FoundStruct;
		}

		FString CandidatePath = StructTypeString;

		if (CandidatePath.StartsWith(TEXT("/Script/")))
		{
			if (UScriptStruct* LoadedStruct = LoadObject<UScriptStruct>(nullptr, *CandidatePath))
			{
				return LoadedStruct;
			}
		}

		if (IsLikelyGeneratedStructPath(CandidatePath))
		{
			CandidatePath += TEXT(".") + FPaths::GetBaseFilename(CandidatePath);
		}

		if (CandidatePath.StartsWith(TEXT("/")))
		{
			if (UScriptStruct* LoadedStruct = LoadObject<UScriptStruct>(nullptr, *CandidatePath))
			{
				return LoadedStruct;
			}

			if (UEditorAssetLibrary::DoesAssetExist(CandidatePath))
			{
				if (UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(CandidatePath))
				{
					if (UScriptStruct* AssetStruct = Cast<UScriptStruct>(LoadedAsset))
					{
						return AssetStruct;
					}
				}
			}
		}

		if (CandidatePath.Contains(TEXT(".")) && !CandidatePath.StartsWith(TEXT("/Script/")))
		{
			const FString ScriptPath = FString::Printf(TEXT("/Script/%s"), *CandidatePath);
			if (UScriptStruct* LoadedStruct = LoadObject<UScriptStruct>(nullptr, *ScriptPath))
			{
				return LoadedStruct;
			}
		}

		if (!CandidatePath.StartsWith(TEXT("/")) && !CandidatePath.Contains(TEXT(".")))
		{
			if (UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*CandidatePath))
			{
				return FoundStruct;
			}
		}

		return nullptr;
	}

	bool TryGetStructTypeFromParams(const TSharedPtr<FJsonObject>& Params, UScriptStruct*& OutStructType)
	{
		OutStructType = nullptr;
		if (!Params.IsValid())
		{
			return false;
		}

		FString StructTypeString;
		if (!Params->TryGetStringField(TEXT("struct_type"), StructTypeString))
		{
			if (!Params->TryGetStringField(TEXT("struct_path"), StructTypeString) &&
				!Params->TryGetStringField(TEXT("struct_name"), StructTypeString))
			{
				return false;
			}
		}

		OutStructType = ResolveStructType(StructTypeString);
		return OutStructType != nullptr;
	}
}

UK2Node* FDataNodeCreator::CreateVariableGetNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
	{
		return nullptr;
	}

	UK2Node_VariableGet* VarGetNode = NewObject<UK2Node_VariableGet>(Graph);
	if (!VarGetNode)
	{
		return nullptr;
	}

	VarGetNode->VariableReference.SetSelfMember(FName(*VariableName));

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	VarGetNode->NodePosX = static_cast<int32>(PosX);
	VarGetNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(VarGetNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(VarGetNode, Graph);

	return VarGetNode;
}

UK2Node* FDataNodeCreator::CreateVariableSetNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
	{
		return nullptr;
	}

	UK2Node_VariableSet* VarSetNode = NewObject<UK2Node_VariableSet>(Graph);
	if (!VarSetNode)
	{
		return nullptr;
	}

	VarSetNode->VariableReference.SetSelfMember(FName(*VariableName));

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	VarSetNode->NodePosX = static_cast<int32>(PosX);
	VarSetNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(VarSetNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(VarSetNode, Graph);

	return VarSetNode;
}


UK2Node* FDataNodeCreator::CreateMakeArrayNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_MakeArray* MakeArrayNode = NewObject<UK2Node_MakeArray>(Graph);
	if (!MakeArrayNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	MakeArrayNode->NodePosX = static_cast<int32>(PosX);
	MakeArrayNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MakeArrayNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(MakeArrayNode, Graph);

	return MakeArrayNode;
}

UK2Node* FDataNodeCreator::CreateMakeStructNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UScriptStruct* StructType = nullptr;
	if (!TryGetStructTypeFromParams(Params, StructType) || !StructType)
	{
		return nullptr;
	}

	UK2Node_MakeStruct* MakeStructNode = NewObject<UK2Node_MakeStruct>(Graph);
	if (!MakeStructNode)
	{
		return nullptr;
	}

	MakeStructNode->StructType = StructType;

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	MakeStructNode->NodePosX = static_cast<int32>(PosX);
	MakeStructNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MakeStructNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(MakeStructNode, Graph);

	return MakeStructNode;
}

UK2Node* FDataNodeCreator::CreateBreakStructNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UScriptStruct* StructType = nullptr;
	if (!TryGetStructTypeFromParams(Params, StructType) || !StructType)
	{
		return nullptr;
	}

	UK2Node_BreakStruct* BreakStructNode = NewObject<UK2Node_BreakStruct>(Graph);
	if (!BreakStructNode)
	{
		return nullptr;
	}

	BreakStructNode->StructType = StructType;

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	BreakStructNode->NodePosX = static_cast<int32>(PosX);
	BreakStructNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(BreakStructNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(BreakStructNode, Graph);

	return BreakStructNode;
}

UK2Node* FDataNodeCreator::CreateSetFieldsInStructNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UScriptStruct* StructType = nullptr;
	if (!TryGetStructTypeFromParams(Params, StructType) || !StructType)
	{
		return nullptr;
	}

	UK2Node_SetFieldsInStruct* SetFieldsNode = NewObject<UK2Node_SetFieldsInStruct>(Graph);
	if (!SetFieldsNode)
	{
		return nullptr;
	}

	SetFieldsNode->StructType = StructType;

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SetFieldsNode->NodePosX = static_cast<int32>(PosX);
	SetFieldsNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SetFieldsNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SetFieldsNode, Graph);

	return SetFieldsNode;
}

