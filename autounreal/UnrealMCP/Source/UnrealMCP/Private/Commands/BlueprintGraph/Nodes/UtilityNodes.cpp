#include "Commands/BlueprintGraph/Nodes/UtilityNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Select.h"
#include "K2Node_SpawnActorFromClass.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Json.h"

static FString NormalizeFunctionName(const FString& InName)
{
	FString OutName = InName;
	OutName.ReplaceInline(TEXT(" "), TEXT(""));
	return OutName;
}

static UClass* ResolveClassFromName(const FString& ClassName)
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	// If already a path, try load directly
	if (ClassName.StartsWith(TEXT("/")))
	{
		return Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *ClassName));
	}

	FString Name = ClassName;
	// If provided like "Engine.Pawn" or "Script/Engine.Pawn"
	if (Name.Contains(TEXT(".")))
	{
		const FString Path = FString::Printf(TEXT("/Script/%s"), *Name);
		if (UClass* Loaded = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *Path)))
		{
			return Loaded;
		}
	}

	// Try direct find by name (any package)
	if (UClass* Found = FindFirstObject<UClass>(*Name))
	{
		return Found;
	}

	// Try with common prefixes/suffixes and engine script path
	FString TryName = Name;
	if (!TryName.StartsWith(TEXT("A")) && !TryName.StartsWith(TEXT("U")))
	{
		// Try Actor-style and UObject-style names
		if (UClass* FoundA = FindFirstObject<UClass>(*(TEXT("A") + TryName)))
		{
			return FoundA;
		}
		if (UClass* FoundU = FindFirstObject<UClass>(*(TEXT("U") + TryName)))
		{
			return FoundU;
		}
	}

	// Try /Script/Engine.Name
	{
		const FString Path = FString::Printf(TEXT("/Script/Engine.%s"), *Name);
		if (UClass* Loaded = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *Path)))
		{
			return Loaded;
		}
	}

	// Try with A/U prefix on Engine script
	if (!Name.StartsWith(TEXT("A")) && !Name.StartsWith(TEXT("U")))
	{
		const FString PathA = FString::Printf(TEXT("/Script/Engine.A%s"), *Name);
		if (UClass* LoadedA = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *PathA)))
		{
			return LoadedA;
		}
		const FString PathU = FString::Printf(TEXT("/Script/Engine.U%s"), *Name);
		if (UClass* LoadedU = Cast<UClass>(StaticLoadClass(UObject::StaticClass(), nullptr, *PathU)))
		{
			return LoadedU;
		}
	}

	return nullptr;
}

static UFunction* FindFunctionInClass(UClass* InClass, const FString& FunctionName)
{
	if (!InClass)
	{
		return nullptr;
	}

	const FName FuncName(*FunctionName);
	if (UFunction* Found = InClass->FindFunctionByName(FuncName))
	{
		return Found;
	}

	const FString Normalized = NormalizeFunctionName(FunctionName);
	if (!Normalized.Equals(FunctionName, ESearchCase::CaseSensitive))
	{
		if (UFunction* Found = InClass->FindFunctionByName(FName(*Normalized)))
		{
			return Found;
		}
	}

	// Walk super chain
	for (UClass* Super = InClass->GetSuperClass(); Super; Super = Super->GetSuperClass())
	{
		if (UFunction* Found = Super->FindFunctionByName(FuncName))
		{
			return Found;
		}
		if (!Normalized.Equals(FunctionName, ESearchCase::CaseSensitive))
		{
			if (UFunction* Found = Super->FindFunctionByName(FName(*Normalized)))
			{
				return Found;
			}
		}
	}

	return nullptr;
}

UK2Node* FUtilityNodeCreator::CreatePrintNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_CallFunction* PrintNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!PrintNode)
	{
		return nullptr;
	}

	UFunction* PrintFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)
	);

	if (!PrintFunc)
	{
		return nullptr;
	}

	// Set function reference BEFORE initialization
	PrintNode->SetFromFunction(PrintFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	PrintNode->NodePosX = static_cast<int32>(PosX);
	PrintNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(PrintNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(PrintNode, Graph);

	// Set message if provided AFTER initialization
	FString Message;
	if (Params->TryGetStringField(TEXT("message"), Message))
	{
		UEdGraphPin* InStringPin = PrintNode->FindPin(TEXT("InString"));
		if (InStringPin)
		{
			InStringPin->DefaultValue = Message;
		}
	}

	return PrintNode;
}

UK2Node* FUtilityNodeCreator::CreateCallFunctionNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	// Get target function name
	FString TargetFunction;
	if (!Params->TryGetStringField(TEXT("target_function"), TargetFunction))
	{
		return nullptr;
	}

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!CallNode)
	{
		return nullptr;
	}

	// Find the function to call
	UFunction* TargetFunc = nullptr;
	FString ClassName;
	if (!Params->TryGetStringField(TEXT("target_class"), ClassName))
	{
		// Tool schema uses target_blueprint for class selection
		Params->TryGetStringField(TEXT("target_blueprint"), ClassName);
	}

	if (!ClassName.IsEmpty())
	{
		UClass* TargetClass = ResolveClassFromName(ClassName);
		TargetFunc = FindFunctionInClass(TargetClass, TargetFunction);
	}
	else
	{
		// Try Blueprint class first (self context)
		if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
		{
			UClass* BPClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(BP);
			TargetFunc = FindFunctionInClass(BPClass, TargetFunction);
		}

		// Try common Unreal libraries if still not found
		if (!TargetFunc)
		{
			TargetFunc = FindFunctionInClass(UKismetSystemLibrary::StaticClass(), TargetFunction);
		}
		if (!TargetFunc)
		{
			TargetFunc = FindFunctionInClass(UKismetMathLibrary::StaticClass(), TargetFunction);
		}
		if (!TargetFunc)
		{
			TargetFunc = FindFunctionInClass(UKismetStringLibrary::StaticClass(), TargetFunction);
		}
		if (!TargetFunc)
		{
			TargetFunc = FindFunctionInClass(UKismetArrayLibrary::StaticClass(), TargetFunction);
		}
		if (!TargetFunc)
		{
			TargetFunc = FindFunctionInClass(UGameplayStatics::StaticClass(), TargetFunction);
		}
	}

	if (!TargetFunc)
	{
		return nullptr;
	}

	// Set function reference BEFORE initialization
	CallNode->SetFromFunction(TargetFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	CallNode->NodePosX = static_cast<int32>(PosX);
	CallNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(CallNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(CallNode, Graph);

	return CallNode;
}

UK2Node* FUtilityNodeCreator::CreateSelectNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(Graph);
	if (!SelectNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SelectNode->NodePosX = static_cast<int32>(PosX);
	SelectNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SelectNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SelectNode, Graph);

	return SelectNode;
}

UK2Node* FUtilityNodeCreator::CreateSpawnActorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_SpawnActorFromClass* SpawnActorNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
	if (!SpawnActorNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SpawnActorNode->NodePosX = static_cast<int32>(PosX);
	SpawnActorNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SpawnActorNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SpawnActorNode, Graph);

	return SpawnActorNode;
}

