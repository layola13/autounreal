#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/BlueprintGraph/BPVariables.h"
#include "Commands/BlueprintGraph/Function/FunctionIO.h"
#include "Commands/BlueprintGraph/Function/FunctionManager.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InputKeyDelegateBinding.h"
#include "EnhancedInputActionDelegateBinding.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_Select.h"
#include "K2Node_Tunnel.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "IPythonScriptPlugin.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
#include "UObject/UnrealType.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/TokenizedMessage.h"
#include "Algo/Unique.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/Interface.h"
#include "EdGraphUtilities.h"
#include "ExportBlueprintToTxtLibrary.h"
#include "BPDirectExporter.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

namespace
{
UEdGraph* FindBlueprintGraphByNameOrPath(UBlueprint* Blueprint, const FString& GraphIdentifier)
{
    if (!Blueprint || GraphIdentifier.IsEmpty())
    {
        return nullptr;
    }

    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph && Graph->GetName().Equals(GraphIdentifier, ESearchCase::IgnoreCase))
        {
            return Graph;
        }
    }

    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph && Graph->GetPathName().Equals(GraphIdentifier, ESearchCase::IgnoreCase))
        {
            return Graph;
        }
    }

    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph && Graph->GetPathName().EndsWith(GraphIdentifier, ESearchCase::IgnoreCase))
        {
            return Graph;
        }
    }

    return nullptr;
}

TArray<FString> GetAllBlueprintGraphNames(UBlueprint* Blueprint)
{
    TArray<FString> GraphNames;
    if (!Blueprint)
    {
        return GraphNames;
    }

    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    GraphNames.Reserve(AllGraphs.Num());

    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph)
        {
            GraphNames.Add(Graph->GetName());
        }
    }

    GraphNames.Sort();
    GraphNames.SetNum(Algo::Unique(GraphNames));

    return GraphNames;
}

FString ResolveBlueprintTargetParam(const TSharedPtr<FJsonObject>& Params)
{
    FString Target;
    if (!Params.IsValid())
    {
        return Target;
    }

    if (!Params->TryGetStringField(TEXT("target"), Target))
    {
        Params->TryGetStringField(TEXT("blueprint_path"), Target);
        if (Target.IsEmpty())
        {
            Params->TryGetStringField(TEXT("asset_path"), Target);
        }
        if (Target.IsEmpty())
        {
            Params->TryGetStringField(TEXT("soft_path_from_project_root"), Target);
        }
        if (Target.IsEmpty())
        {
            Params->TryGetStringField(TEXT("blueprint_name"), Target);
        }
        if (Target.IsEmpty())
        {
            Params->TryGetStringField(TEXT("blueprint"), Target);
        }
    }

    Target.TrimStartAndEndInline();
    return Target;
}

bool TryDeserializeJsonObject_BP(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
{
    OutObject.Reset();

    FString Trimmed = JsonText;
    Trimmed.TrimStartAndEndInline();
    if (Trimmed.IsEmpty())
    {
        return false;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
    return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

bool TryDeserializeJsonArray_BP(const FString& JsonText, TArray<TSharedPtr<FJsonValue>>& OutArray)
{
    OutArray.Reset();

    FString Trimmed = JsonText;
    Trimmed.TrimStartAndEndInline();
    if (Trimmed.IsEmpty())
    {
        return false;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
    return FJsonSerializer::Deserialize(Reader, OutArray);
}

TSharedPtr<FJsonObject> GetRelevantDataObject_BP(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
    OutError.Empty();

    if (!Params.IsValid())
    {
        return MakeShared<FJsonObject>();
    }

    const TSharedPtr<FJsonObject>* RelevantDataObject = nullptr;
    if (Params->TryGetObjectField(TEXT("relevant_data"), RelevantDataObject) && RelevantDataObject && RelevantDataObject->IsValid())
    {
        return *RelevantDataObject;
    }

    FString RelevantDataString;
    if (Params->TryGetStringField(TEXT("relevant_data"), RelevantDataString))
    {
        TSharedPtr<FJsonObject> ParsedObject;
        if (TryDeserializeJsonObject_BP(RelevantDataString, ParsedObject))
        {
            return ParsedObject;
        }

        OutError = TEXT("Failed to parse 'relevant_data' JSON object");
    }

    return MakeShared<FJsonObject>();
}

void CopyJsonFieldIfPresent_BP(
    const TSharedPtr<FJsonObject>& Source,
    const TSharedPtr<FJsonObject>& Target,
    const TCHAR* SourceField,
    const TCHAR* TargetField = nullptr)
{
    if (!Source.IsValid() || !Target.IsValid())
    {
        return;
    }

    const TSharedPtr<FJsonValue> Value = Source->Values.FindRef(SourceField);
    if (Value.IsValid())
    {
        Target->SetField(TargetField ? TargetField : SourceField, Value);
    }
}

void CopyAllJsonFields_BP(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Target)
{
    if (!Source.IsValid() || !Target.IsValid())
    {
        return;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Source->Values)
    {
        Target->SetField(Pair.Key, Pair.Value);
    }
}

FString GetFirstNonEmptyStringField_BP(const TSharedPtr<FJsonObject>& Source, const TArray<FString>& FieldNames)
{
    if (!Source.IsValid())
    {
        return FString();
    }

    for (const FString& FieldName : FieldNames)
    {
        FString Value;
        if (Source->TryGetStringField(FieldName, Value))
        {
            Value.TrimStartAndEndInline();
            if (!Value.IsEmpty())
            {
                return Value;
            }
        }
    }

    return FString();
}

TArray<FString> GetStringArrayField_BP(const TSharedPtr<FJsonObject>& Source, const TCHAR* FieldName)
{
    TArray<FString> Results;
    if (!Source.IsValid())
    {
        return Results;
    }

    const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
    if (!Source->TryGetArrayField(FieldName, JsonArray) || !JsonArray)
    {
        return Results;
    }

    for (const TSharedPtr<FJsonValue>& JsonValue : *JsonArray)
    {
        FString Value;
        if (JsonValue.IsValid() && JsonValue->TryGetString(Value))
        {
            Value.TrimStartAndEndInline();
            if (!Value.IsEmpty())
            {
                Results.Add(Value);
            }
        }
    }

    return Results;
}

bool HasNonEmptyJsonArrayField_BP(const TSharedPtr<FJsonObject>& Source, const TCHAR* FieldName)
{
    if (!Source.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
    return Source->TryGetArrayField(FieldName, JsonArray) && JsonArray && JsonArray->Num() > 0;
}

bool HasObjectField_BP(const TSharedPtr<FJsonObject>& Source, const TCHAR* FieldName)
{
    if (!Source.IsValid())
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* JsonObject = nullptr;
    return Source->TryGetObjectField(FieldName, JsonObject) && JsonObject && JsonObject->IsValid();
}

FString ResolveBlueprintTargetFromRelevantData_BP(const TSharedPtr<FJsonObject>& RelevantData)
{
    FString Target = GetFirstNonEmptyStringField_BP(
        RelevantData,
        {
            TEXT("target"),
            TEXT("blueprint_path"),
            TEXT("asset_path"),
            TEXT("soft_path_from_project_root"),
            TEXT("blueprint_name"),
            TEXT("blueprint")
        });

    if (!Target.IsEmpty())
    {
        return Target;
    }

    const TArray<FString> BlueprintPaths = GetStringArrayField_BP(RelevantData, TEXT("blueprint_paths"));
    if (BlueprintPaths.Num() > 0)
    {
        return BlueprintPaths[0];
    }

    const TArray<TSharedPtr<FJsonValue>>* GraphInfoArray = nullptr;
    if (RelevantData.IsValid() && RelevantData->TryGetArrayField(TEXT("graph_info"), GraphInfoArray) && GraphInfoArray)
    {
        for (const TSharedPtr<FJsonValue>& GraphInfoValue : *GraphInfoArray)
        {
            if (!GraphInfoValue.IsValid() || GraphInfoValue->Type != EJson::Object)
            {
                continue;
            }

            const TSharedPtr<FJsonObject> GraphInfo = GraphInfoValue->AsObject();
            Target = GetFirstNonEmptyStringField_BP(
                GraphInfo,
                {
                    TEXT("blueprint_path"),
                    TEXT("asset_path"),
                    TEXT("soft_path_from_project_root"),
                    TEXT("target"),
                    TEXT("blueprint_name")
                });

            if (!Target.IsEmpty())
            {
                return Target;
            }
        }
    }

    return FString();
}

FString ResolvePrimaryGraphName_BP(const TSharedPtr<FJsonObject>& RelevantData)
{
    FString GraphName = GetFirstNonEmptyStringField_BP(
        RelevantData,
        {
            TEXT("graph_name"),
            TEXT("graph_page"),
            TEXT("strand_name")
        });

    if (!GraphName.IsEmpty())
    {
        return GraphName;
    }

    const TArray<TSharedPtr<FJsonValue>>* GraphInfoArray = nullptr;
    if (RelevantData.IsValid() && RelevantData->TryGetArrayField(TEXT("graph_info"), GraphInfoArray) && GraphInfoArray)
    {
        for (const TSharedPtr<FJsonValue>& GraphInfoValue : *GraphInfoArray)
        {
            if (!GraphInfoValue.IsValid() || GraphInfoValue->Type != EJson::Object)
            {
                continue;
            }

            GraphName = GetFirstNonEmptyStringField_BP(
                GraphInfoValue->AsObject(),
                {
                    TEXT("graph_name"),
                    TEXT("graph_page"),
                    TEXT("strand_name")
                });

            if (!GraphName.IsEmpty())
            {
                return GraphName;
            }
        }
    }

    return TEXT("EventGraph");
}

bool RequestTextLooksCompileOnly_BP(const FString& RequestText)
{
    const FString Normalized = RequestText.ToLower();
    return Normalized.Contains(TEXT("compile")) ||
        Normalized.Contains(TEXT("recompile")) ||
        RequestText.Contains(TEXT("编译")) ||
        RequestText.Contains(TEXT("重新编译"));
}

bool HasActionableBlueprintEditFields_BP(const TSharedPtr<FJsonObject>& Source)
{
    return HasObjectField_BP(Source, TEXT("edit_blueprint_params")) ||
        HasObjectField_BP(Source, TEXT("blueprint_properties")) ||
        HasObjectField_BP(Source, TEXT("properties")) ||
        HasNonEmptyJsonArrayField_BP(Source, TEXT("components")) ||
        HasNonEmptyJsonArrayField_BP(Source, TEXT("variables")) ||
        HasNonEmptyJsonArrayField_BP(Source, TEXT("functions")) ||
        HasNonEmptyJsonArrayField_BP(Source, TEXT("interfaces"));
}

void CopyBlueprintEditFields_BP(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Target)
{
    if (!Source.IsValid() || !Target.IsValid())
    {
        return;
    }

    const TSharedPtr<FJsonObject>* NestedEditParams = nullptr;
    if (Source->TryGetObjectField(TEXT("edit_blueprint_params"), NestedEditParams) && NestedEditParams && NestedEditParams->IsValid())
    {
        CopyAllJsonFields_BP(*NestedEditParams, Target);
    }

    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("blueprint_properties"));
    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("properties"), TEXT("blueprint_properties"));
    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("components"));
    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("variables"));
    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("functions"));
    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("interfaces"));
    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("continue_on_error"));
    CopyJsonFieldIfPresent_BP(Source, Target, TEXT("compile_blueprint"));
}

#if PLATFORM_WINDOWS
FString LiveCodingCompileResultToString(const ELiveCodingCompileResult Result)
{
    switch (Result)
    {
    case ELiveCodingCompileResult::Success:
        return TEXT("Success");
    case ELiveCodingCompileResult::NoChanges:
        return TEXT("NoChanges");
    case ELiveCodingCompileResult::InProgress:
        return TEXT("InProgress");
    case ELiveCodingCompileResult::CompileStillActive:
        return TEXT("CompileStillActive");
    case ELiveCodingCompileResult::NotStarted:
        return TEXT("NotStarted");
    case ELiveCodingCompileResult::Failure:
        return TEXT("Failure");
    case ELiveCodingCompileResult::Cancelled:
        return TEXT("Cancelled");
    default:
        return TEXT("Unknown");
    }
}

struct FLiveCodingLogFileCandidate
{
    FString Path;
    FDateTime Timestamp;
};

bool IsProjectEditorLogFile(const FString& FilePath)
{
    const FString FileName = FPaths::GetCleanFilename(FilePath);
    return FileName.EndsWith(TEXT(".log"), ESearchCase::IgnoreCase) &&
        !FileName.StartsWith(TEXT("cef"), ESearchCase::IgnoreCase);
}

TArray<FLiveCodingLogFileCandidate> GetRecentProjectLogFiles()
{
    const FString LogsDirectory = FPaths::Combine(FPaths::ProjectLogDir());
    TArray<FLiveCodingLogFileCandidate> Candidates;

    if (!IFileManager::Get().DirectoryExists(*LogsDirectory))
    {
        return Candidates;
    }

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(FPaths::Combine(LogsDirectory, TEXT("*.log"))), true, false);
    for (const FString& FileName : Files)
    {
        const FString FullPath = FPaths::Combine(LogsDirectory, FileName);
        if (!IsProjectEditorLogFile(FullPath))
        {
            continue;
        }

        FLiveCodingLogFileCandidate Candidate;
        Candidate.Path = FullPath;
        Candidate.Timestamp = IFileManager::Get().GetTimeStamp(*FullPath);
        Candidates.Add(MoveTemp(Candidate));
    }

    Candidates.Sort([](const FLiveCodingLogFileCandidate& Left, const FLiveCodingLogFileCandidate& Right)
    {
        return Left.Timestamp > Right.Timestamp;
    });

    return Candidates;
}

bool ExtractLatestLiveCodingBlock(
    const FString& InLogPath,
    const int32 MaxLines,
    TArray<FString>& OutLines,
    int32& OutStartLineNumber,
    FString& OutSummary)
{
    FString FileContents;
    if (!FFileHelper::LoadFileToString(FileContents, *InLogPath))
    {
        return false;
    }

    TArray<FString> Lines;
    FileContents.ParseIntoArrayLines(Lines, false);
    if (Lines.Num() == 0)
    {
        return false;
    }

    int32 LastLiveCodingLine = INDEX_NONE;
    int32 LastCompileStartLine = INDEX_NONE;
    int32 LastCompletionLine = INDEX_NONE;

    for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
    {
        const FString& Line = Lines[LineIndex];
        if (!Line.Contains(TEXT("LogLiveCoding"), ESearchCase::IgnoreCase) &&
            !Line.Contains(TEXT("Live Coding"), ESearchCase::IgnoreCase) &&
            !Line.Contains(TEXT("LiveCoding"), ESearchCase::IgnoreCase))
        {
            continue;
        }

        LastLiveCodingLine = LineIndex;

        if (Line.Contains(TEXT("Starting Live Coding compile"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Starting LiveCoding"), ESearchCase::IgnoreCase))
        {
            LastCompileStartLine = LineIndex;
        }

        if (Line.Contains(TEXT("Live coding succeeded"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Live coding failed"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Compile success"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Compile error"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Link error"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Patch creation successful"), ESearchCase::IgnoreCase))
        {
            LastCompletionLine = LineIndex;
        }
    }

    if (LastLiveCodingLine == INDEX_NONE)
    {
        return false;
    }

    int32 StartLine = LastCompileStartLine != INDEX_NONE ? LastCompileStartLine : LastLiveCodingLine;
    int32 EndLine = LastCompletionLine != INDEX_NONE && LastCompletionLine >= StartLine
        ? FMath::Min(LastCompletionLine + 12, Lines.Num() - 1)
        : Lines.Num() - 1;

    if (EndLine - StartLine + 1 > MaxLines)
    {
        StartLine = EndLine - MaxLines + 1;
    }

    for (int32 LineIndex = StartLine; LineIndex <= EndLine; ++LineIndex)
    {
        OutLines.Add(Lines[LineIndex]);
    }

    OutStartLineNumber = StartLine + 1;

    OutSummary = TEXT("Latest Live Coding log block extracted.");
    for (const FString& Line : OutLines)
    {
        if (Line.Contains(TEXT("Live coding failed"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Compile error"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Link error"), ESearchCase::IgnoreCase))
        {
            OutSummary = TEXT("Latest Live Coding compile ended with an error.");
            break;
        }
        if (Line.Contains(TEXT("no code changes detected"), ESearchCase::IgnoreCase))
        {
            OutSummary = TEXT("Latest Live Coding compile reported no code changes.");
            break;
        }
        if (Line.Contains(TEXT("Live coding succeeded"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Compile success"), ESearchCase::IgnoreCase) ||
            Line.Contains(TEXT("Patch creation successful"), ESearchCase::IgnoreCase))
        {
            OutSummary = TEXT("Latest Live Coding compile succeeded.");
            break;
        }
    }

    return true;
}
#endif

struct FCopyValueEndpoint
{
    FString BlueprintName;
    FString ComponentName;
    FString PropertyName;
};

bool ParseCopyValueEndpoint(const TSharedPtr<FJsonObject>& EndpointJson, const FString& EndpointLabel, FCopyValueEndpoint& OutEndpoint, FString& OutError)
{
    if (!EndpointJson.IsValid())
    {
        OutError = FString::Printf(TEXT("Missing '%s' object"), *EndpointLabel);
        return false;
    }

    bool bHasBlueprint = EndpointJson->TryGetStringField(TEXT("blueprint_name"), OutEndpoint.BlueprintName);
    if (!bHasBlueprint)
    {
        bHasBlueprint = EndpointJson->TryGetStringField(TEXT("blueprint_path"), OutEndpoint.BlueprintName);
    }
    if (!bHasBlueprint)
    {
        bHasBlueprint = EndpointJson->TryGetStringField(TEXT("blueprint"), OutEndpoint.BlueprintName);
    }

    if (!bHasBlueprint || OutEndpoint.BlueprintName.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Missing '%s.blueprint_name'"), *EndpointLabel);
        return false;
    }

    bool bHasProperty = EndpointJson->TryGetStringField(TEXT("property_name"), OutEndpoint.PropertyName);
    if (!bHasProperty)
    {
        bHasProperty = EndpointJson->TryGetStringField(TEXT("property"), OutEndpoint.PropertyName);
    }

    if (!bHasProperty || OutEndpoint.PropertyName.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Missing '%s.property_name'"), *EndpointLabel);
        return false;
    }

    EndpointJson->TryGetStringField(TEXT("component_name"), OutEndpoint.ComponentName);
    if (OutEndpoint.ComponentName.IsEmpty())
    {
        EndpointJson->TryGetStringField(TEXT("component"), OutEndpoint.ComponentName);
    }

    return true;
}

UObject* ResolveBlueprintEndpointObject(UBlueprint* Blueprint, const FString& ComponentName)
{
    if (!Blueprint)
    {
        return nullptr;
    }

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintGeneratedClassSafe(Blueprint);
    if (!BlueprintClass)
    {
        return nullptr;
    }

    UObject* CDO = BlueprintClass->GetDefaultObject();
    if (!CDO)
    {
        return nullptr;
    }

    if (ComponentName.IsEmpty())
    {
        return CDO;
    }

    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node || !Node->ComponentTemplate)
            {
                continue;
            }

            const bool bVarNameMatch = Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase);
            const bool bNodeNameMatch = Node->GetName().Equals(ComponentName, ESearchCase::IgnoreCase);
            const bool bObjectNameMatch = Node->ComponentTemplate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase);
            if (bVarNameMatch || bNodeNameMatch || bObjectNameMatch)
            {
                return Node->ComponentTemplate;
            }
        }
    }

    if (AActor* ActorCDO = Cast<AActor>(CDO))
    {
        TArray<UActorComponent*> Components;
        ActorCDO->GetComponents(Components);
        for (UActorComponent* Component : Components)
        {
            if (!Component)
            {
                continue;
            }

            if (Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
            {
                return Component;
            }
        }
    }

    for (TFieldIterator<FObjectPropertyBase> It(CDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FObjectPropertyBase* ObjectProperty = *It;
        if (!ObjectProperty)
        {
            continue;
        }

        if (!ObjectProperty->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
        {
            continue;
        }

        if (UObject* PropertyObject = ObjectProperty->GetObjectPropertyValue_InContainer(CDO))
        {
            return PropertyObject;
        }
    }

    return nullptr;
}

FProperty* FindPropertyByNameLoose(UClass* Class, const FString& PropertyName)
{
    if (!Class || PropertyName.IsEmpty())
    {
        return nullptr;
    }

    if (FProperty* ExactProperty = Class->FindPropertyByName(*PropertyName))
    {
        return ExactProperty;
    }

    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (Property && Property->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
        {
            return Property;
        }
    }

    return nullptr;
}

bool TryReadIntegralProperty(const FProperty* Property, const void* ValuePtr, int64& OutValue)
{
    if (!Property || !ValuePtr)
    {
        return false;
    }

    if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
    {
        const FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty();
        if (Underlying)
        {
            OutValue = Underlying->GetSignedIntPropertyValue(ValuePtr);
            return true;
        }
        return false;
    }

    if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
    {
        OutValue = ByteProperty->GetPropertyValue(ValuePtr);
        return true;
    }

    if (const FNumericProperty* NumericProperty = CastField<const FNumericProperty>(Property))
    {
        if (NumericProperty->IsInteger())
        {
            OutValue = NumericProperty->GetSignedIntPropertyValue(ValuePtr);
            return true;
        }
    }

    return false;
}

bool TryWriteIntegralProperty(FProperty* Property, void* ValuePtr, int64 InValue)
{
    if (!Property || !ValuePtr)
    {
        return false;
    }

    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        FNumericProperty* Underlying = EnumProperty->GetUnderlyingProperty();
        if (Underlying)
        {
            Underlying->SetIntPropertyValue(ValuePtr, InValue);
            return true;
        }
        return false;
    }

    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(InValue));
        return true;
    }

    if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        if (NumericProperty->IsInteger())
        {
            NumericProperty->SetIntPropertyValue(ValuePtr, InValue);
            return true;
        }
    }

    return false;
}

bool TryCopyPropertyValue(const FProperty* SourceProperty, const void* SourceValuePtr, FProperty* TargetProperty, void* TargetValuePtr, FString& OutError)
{
    if (!SourceProperty || !TargetProperty || !SourceValuePtr || !TargetValuePtr)
    {
        OutError = TEXT("Invalid property/value pointer");
        return false;
    }

    if (SourceProperty->SameType(TargetProperty))
    {
        TargetProperty->CopyCompleteValue(TargetValuePtr, SourceValuePtr);
        return true;
    }

    if (const FBoolProperty* SourceBool = CastField<const FBoolProperty>(SourceProperty))
    {
        if (FBoolProperty* TargetBool = CastField<FBoolProperty>(TargetProperty))
        {
            TargetBool->SetPropertyValue(TargetValuePtr, SourceBool->GetPropertyValue(SourceValuePtr));
            return true;
        }
    }

    if (const FObjectPropertyBase* SourceObject = CastField<const FObjectPropertyBase>(SourceProperty))
    {
        if (FObjectPropertyBase* TargetObject = CastField<FObjectPropertyBase>(TargetProperty))
        {
            UObject* SourceObjectValue = SourceObject->GetObjectPropertyValue(SourceValuePtr);
            if (!SourceObjectValue || SourceObjectValue->IsA(TargetObject->PropertyClass))
            {
                TargetObject->SetObjectPropertyValue(TargetValuePtr, SourceObjectValue);
                return true;
            }

            OutError = FString::Printf(TEXT("Object type mismatch: '%s' is not assignable to '%s'"),
                *SourceObjectValue->GetClass()->GetName(),
                *TargetObject->PropertyClass->GetName());
            return false;
        }
    }

    if (const FNumericProperty* SourceNumeric = CastField<const FNumericProperty>(SourceProperty))
    {
        if (FNumericProperty* TargetNumeric = CastField<FNumericProperty>(TargetProperty))
        {
            if (SourceNumeric->IsInteger())
            {
                const int64 Value = SourceNumeric->GetSignedIntPropertyValue(SourceValuePtr);
                if (TargetNumeric->IsInteger())
                {
                    TargetNumeric->SetIntPropertyValue(TargetValuePtr, Value);
                }
                else
                {
                    TargetNumeric->SetFloatingPointPropertyValue(TargetValuePtr, static_cast<double>(Value));
                }
            }
            else
            {
                const double Value = SourceNumeric->GetFloatingPointPropertyValue(SourceValuePtr);
                if (TargetNumeric->IsInteger())
                {
                    TargetNumeric->SetIntPropertyValue(TargetValuePtr, static_cast<int64>(FMath::RoundToInt64(Value)));
                }
                else
                {
                    TargetNumeric->SetFloatingPointPropertyValue(TargetValuePtr, Value);
                }
            }
            return true;
        }
    }

    int64 EnumIntegralValue = 0;
    if (TryReadIntegralProperty(SourceProperty, SourceValuePtr, EnumIntegralValue))
    {
        if (TryWriteIntegralProperty(TargetProperty, TargetValuePtr, EnumIntegralValue))
        {
            return true;
        }
    }

    if (const FStructProperty* SourceStruct = CastField<const FStructProperty>(SourceProperty))
    {
        if (FStructProperty* TargetStruct = CastField<FStructProperty>(TargetProperty))
        {
            if (SourceStruct->Struct == TargetStruct->Struct)
            {
                TargetStruct->CopyCompleteValue(TargetValuePtr, SourceValuePtr);
                return true;
            }

            OutError = FString::Printf(TEXT("Struct type mismatch: %s -> %s"),
                *SourceStruct->Struct->GetName(),
                *TargetStruct->Struct->GetName());
            return false;
        }
    }

    OutError = FString::Printf(TEXT("Unsupported conversion: %s -> %s"),
        *SourceProperty->GetClass()->GetName(),
        *TargetProperty->GetClass()->GetName());
    return false;
}

FString NormalizeReferenceString(const FString& InRef)
{
    FString Result = InRef;
    Result.TrimStartAndEndInline();

    if ((Result.StartsWith(TEXT("\"")) && Result.EndsWith(TEXT("\""))) ||
        (Result.StartsWith(TEXT("'")) && Result.EndsWith(TEXT("'"))))
    {
        Result = Result.Mid(1, Result.Len() - 2);
    }

    int32 QuoteStart = INDEX_NONE;
    if (Result.FindChar(TEXT('\''), QuoteStart))
    {
        int32 QuoteEnd = INDEX_NONE;
        if (Result.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
        {
            Result = Result.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
        }
    }

    Result.TrimStartAndEndInline();
    return Result;
}

bool DoesReferenceMatch(const FString& CandidatePath, const FString& OldRefRaw, const FString& OldRefNormalized)
{
    if (CandidatePath.IsEmpty())
    {
        return false;
    }

    if (CandidatePath.Equals(OldRefRaw, ESearchCase::IgnoreCase))
    {
        return true;
    }

    const FString NormalizedCandidate = NormalizeReferenceString(CandidatePath);
    return NormalizedCandidate.Equals(OldRefNormalized, ESearchCase::IgnoreCase);
}

UObject* ResolveObjectByReference(const FString& InRef)
{
    TArray<FString> Candidates;
    Candidates.Add(InRef);

    const FString Normalized = NormalizeReferenceString(InRef);
    if (!Normalized.IsEmpty() && !Candidates.Contains(Normalized))
    {
        Candidates.Add(Normalized);
    }

    if (!Normalized.IsEmpty())
    {
        const FString WrappedGeneratedClass = FString::Printf(TEXT("/Script/Engine.BlueprintGeneratedClass'%s'"), *Normalized);
        if (!Candidates.Contains(WrappedGeneratedClass))
        {
            Candidates.Add(WrappedGeneratedClass);
        }

        const FString WrappedClass = FString::Printf(TEXT("/Script/CoreUObject.Class'%s'"), *Normalized);
        if (!Candidates.Contains(WrappedClass))
        {
            Candidates.Add(WrappedClass);
        }
    }

    for (const FString& Candidate : Candidates)
    {
        if (Candidate.IsEmpty())
        {
            continue;
        }

        if (UObject* Found = StaticLoadObject(UObject::StaticClass(), nullptr, *Candidate))
        {
            return Found;
        }
    }

    return nullptr;
}

UClass* ResolveClassByReference(const FString& InRef)
{
    if (InRef.IsEmpty())
    {
        return nullptr;
    }

    if (UObject* Obj = ResolveObjectByReference(InRef))
    {
        if (UClass* ObjClass = Cast<UClass>(Obj))
        {
            return ObjClass;
        }

        if (UBlueprint* ObjBlueprint = Cast<UBlueprint>(Obj))
        {
            return FEpicUnrealMCPCommonUtils::GetBlueprintGeneratedClassSafe(ObjBlueprint);
        }
    }

    const FString Normalized = NormalizeReferenceString(InRef);
    if (!Normalized.IsEmpty())
    {
        if (UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(Normalized))
        {
            return FEpicUnrealMCPCommonUtils::GetBlueprintGeneratedClassSafe(Blueprint);
        }
    }

    return nullptr;
}

bool TryParseFlexibleBoolField(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, bool& OutValue, FString& OutError)
{
    if (!Params.IsValid())
    {
        OutError = TEXT("Invalid params object");
        return false;
    }

    if (Params->TryGetBoolField(FieldName, OutValue))
    {
        return true;
    }

    if (Params->HasTypedField<EJson::Number>(FieldName))
    {
        OutValue = Params->GetNumberField(FieldName) != 0.0;
        return true;
    }

    FString ToggleText;
    if (Params->TryGetStringField(FieldName, ToggleText))
    {
        ToggleText.TrimStartAndEndInline();
        ToggleText.ToLowerInline();

        if (ToggleText == TEXT("true") || ToggleText == TEXT("1") || ToggleText == TEXT("on") || ToggleText == TEXT("enable") || ToggleText == TEXT("enabled"))
        {
            OutValue = true;
            return true;
        }

        if (ToggleText == TEXT("false") || ToggleText == TEXT("0") || ToggleText == TEXT("off") || ToggleText == TEXT("disable") || ToggleText == TEXT("disabled"))
        {
            OutValue = false;
            return true;
        }

        OutError = FString::Printf(TEXT("Invalid boolean string for '%s': %s"), *FieldName, *ToggleText);
        return false;
    }

    OutError = FString::Printf(TEXT("Missing '%s' parameter"), *FieldName);
    return false;
}

TSharedPtr<FJsonObject> MakeCompactNodeJson(const UEdGraphNode* Node)
{
    TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
    NodeObj->SetStringField(TEXT("name"), Node ? Node->GetName() : TEXT(""));
    NodeObj->SetStringField(TEXT("class"), Node ? Node->GetClass()->GetName() : TEXT(""));
    NodeObj->SetStringField(TEXT("title"), Node ? Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() : TEXT(""));
    return NodeObj;
}

void AddUniqueCompactConnection(const UEdGraphPin* Pin, const UEdGraphPin* LinkedPin, TArray<TSharedPtr<FJsonValue>>& ConnectionArray, TSet<FString>& SeenConnections)
{
    if (!Pin || !LinkedPin)
    {
        return;
    }

    const UEdGraphNode* PinNode = Pin->GetOwningNode();
    const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
    if (!PinNode || !LinkedNode)
    {
        return;
    }

    const UEdGraphPin* SourcePin = Pin;
    const UEdGraphPin* TargetPin = LinkedPin;

    if (Pin->Direction == EGPD_Input && LinkedPin->Direction == EGPD_Output)
    {
        SourcePin = LinkedPin;
        TargetPin = Pin;
    }
    else if (Pin->Direction == LinkedPin->Direction)
    {
        const FString PinRef = FString::Printf(TEXT("%s.%s"), *PinNode->GetName(), *Pin->PinName.ToString());
        const FString LinkedRef = FString::Printf(TEXT("%s.%s"), *LinkedNode->GetName(), *LinkedPin->PinName.ToString());
        if (PinRef > LinkedRef)
        {
            SourcePin = LinkedPin;
            TargetPin = Pin;
        }
    }

    const UEdGraphNode* SourceNode = SourcePin->GetOwningNode();
    const UEdGraphNode* TargetNode = TargetPin->GetOwningNode();
    if (!SourceNode || !TargetNode)
    {
        return;
    }

    const FString SourceNodeName = SourceNode->GetName();
    const FString SourcePinName = SourcePin->PinName.ToString();
    const FString TargetNodeName = TargetNode->GetName();
    const FString TargetPinName = TargetPin->PinName.ToString();
    const FString ConnectionKey = FString::Printf(TEXT("%s.%s->%s.%s"), *SourceNodeName, *SourcePinName, *TargetNodeName, *TargetPinName);

    if (SeenConnections.Contains(ConnectionKey))
    {
        return;
    }

    SeenConnections.Add(ConnectionKey);

    TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
    ConnObj->SetStringField(TEXT("from_node"), SourceNodeName);
    ConnObj->SetStringField(TEXT("from_pin"), SourcePinName);
    ConnObj->SetStringField(TEXT("to_node"), TargetNodeName);
    ConnObj->SetStringField(TEXT("to_pin"), TargetPinName);
    ConnObj->SetStringField(
        TEXT("kind"),
        SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec ? TEXT("exec") : SourcePin->PinType.PinCategory.ToString());

    ConnectionArray.Add(MakeShared<FJsonValueObject>(ConnObj));
}

struct FBlueprintPropertyQueryOptions
{
    bool bIncludeValues = true;
    bool bIncludeMetadata = true;
    bool bIncludeInherited = true;
    bool bEditableOnly = true;
    TSet<FString> RequestedPropertyNames;
};

struct FBlueprintComponentHandle
{
    FString Name;
    FString Source;
    bool bIsRoot = false;
    bool bIsInherited = false;
    USCS_Node* SCSNode = nullptr;
    UObject* Object = nullptr;
};

enum class EBlueprintMetaValueStyle : uint8
{
    VariableCompact,
    DefaultPropertyVerbose,
    ContainerElementCompact,
};

struct FBlueprintMetaExportOptions
{
    TSet<FString> RequestedParts;
    int32 PropertyValueMaxDepth = 1;
    int32 PropertyValueMaxItems = 16;
    bool bIncludeRawGraphPayload = false;
};

struct FBlueprintMetaParamInfo
{
    FString TypeText;
    FString Name;
    FString DefaultValue;
};

struct FBlueprintMetaFunctionInfo
{
    FString Name;
    TArray<FBlueprintMetaParamInfo> Inputs;
    TArray<FBlueprintMetaParamInfo> Outputs;
};

struct FBlueprintMetaEventInfo
{
    FString Name;
    TArray<FBlueprintMetaParamInfo> Inputs;
    FString ReplicationText;
};

struct FBlueprintMetaComponentInfo
{
    FString Name;
    FString ClassName;
    FString ParentName;
    int32 OrderIndex = INDEX_NONE;
};

FString NormalizeLookupKey(const FString& InValue)
{
    FString Result = InValue;
    Result.TrimStartAndEndInline();
    Result.ToLowerInline();
    return Result;
}

FString NormalizeMetaPartName_BP(const FString& InValue)
{
    return NormalizeLookupKey(InValue);
}

void BuildMetaExportOptionsFromParams_BP(const TSharedPtr<FJsonObject>& Params, FBlueprintMetaExportOptions& OutOptions)
{
    OutOptions.RequestedParts.Reset();
    OutOptions.PropertyValueMaxDepth = 1;
    OutOptions.PropertyValueMaxItems = 16;
    OutOptions.bIncludeRawGraphPayload = false;

    const TArray<TSharedPtr<FJsonValue>>* PartsArray = nullptr;
    if (Params.IsValid() && Params->TryGetArrayField(TEXT("parts"), PartsArray) && PartsArray)
    {
        for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
        {
            if (!PartValue.IsValid())
            {
                continue;
            }

            FString PartName;
            switch (PartValue->Type)
            {
            case EJson::String:
                PartName = PartValue->AsString();
                break;

            case EJson::Number:
                PartName = FString::SanitizeFloat(PartValue->AsNumber());
                break;

            case EJson::Boolean:
                PartName = PartValue->AsBool() ? TEXT("true") : TEXT("false");
                break;

            default:
                break;
            }

            if (!PartName.IsEmpty())
            {
                OutOptions.RequestedParts.Add(NormalizeMetaPartName_BP(PartName));
            }
        }
    }

    if (OutOptions.RequestedParts.Num() == 0)
    {
        OutOptions.RequestedParts = {
            TEXT("propertydeclarations"),
            TEXT("propertyvalues"),
            TEXT("events"),
            TEXT("functions"),
            TEXT("macros"),
            TEXT("interfaces"),
            TEXT("components"),
            TEXT("graphs"),
        };
    }

    auto TryReadDepthField = [&Params, &OutOptions](const TCHAR* FieldName)
    {
        if (!Params.IsValid() || !Params->HasField(FieldName))
        {
            return false;
        }

        double DepthAsNumber = 0.0;
        if (Params->TryGetNumberField(FieldName, DepthAsNumber))
        {
            OutOptions.PropertyValueMaxDepth = FMath::Clamp(static_cast<int32>(DepthAsNumber), 0, 8);
            return true;
        }

        FString DepthAsString;
        if (Params->TryGetStringField(FieldName, DepthAsString))
        {
            DepthAsString.TrimStartAndEndInline();
            if (!DepthAsString.IsEmpty())
            {
                OutOptions.PropertyValueMaxDepth = FMath::Clamp(FCString::Atoi(*DepthAsString), 0, 8);
                return true;
            }
        }

        return false;
    };

    TryReadDepthField(TEXT("property_value_depth")) ||
        TryReadDepthField(TEXT("propertyvalues_depth")) ||
        TryReadDepthField(TEXT("property_depth")) ||
        TryReadDepthField(TEXT("depth"));

    auto TryReadMaxItemsField = [&Params, &OutOptions](const TCHAR* FieldName)
    {
        if (!Params.IsValid() || !Params->HasField(FieldName))
        {
            return false;
        }

        double MaxItemsAsNumber = 0.0;
        if (Params->TryGetNumberField(FieldName, MaxItemsAsNumber))
        {
            OutOptions.PropertyValueMaxItems = FMath::Clamp(static_cast<int32>(MaxItemsAsNumber), 0, 256);
            return true;
        }

        FString MaxItemsAsString;
        if (Params->TryGetStringField(FieldName, MaxItemsAsString))
        {
            MaxItemsAsString.TrimStartAndEndInline();
            if (!MaxItemsAsString.IsEmpty())
            {
                OutOptions.PropertyValueMaxItems = FMath::Clamp(FCString::Atoi(*MaxItemsAsString), 0, 256);
                return true;
            }
        }

        return false;
    };

    TryReadMaxItemsField(TEXT("property_value_max_items")) ||
        TryReadMaxItemsField(TEXT("propertyvalues_max_items")) ||
        TryReadMaxItemsField(TEXT("property_max_items")) ||
        TryReadMaxItemsField(TEXT("max_items"));

    auto TryReadBoolField = [&Params](const TCHAR* FieldName, bool& OutValue)
    {
        if (!Params.IsValid() || !Params->HasField(FieldName))
        {
            return false;
        }

        FString ParseError;
        return TryParseFlexibleBoolField(Params, FieldName, OutValue, ParseError);
    };

    TryReadBoolField(TEXT("include_raw_graph_payload"), OutOptions.bIncludeRawGraphPayload) ||
        TryReadBoolField(TEXT("raw_graph_payload"), OutOptions.bIncludeRawGraphPayload) ||
        TryReadBoolField(TEXT("include_graph_raw_payload"), OutOptions.bIncludeRawGraphPayload);

    if (OutOptions.RequestedParts.Contains(TEXT("raw_graph_payload")) ||
        OutOptions.RequestedParts.Contains(TEXT("graph_raw_payload")) ||
        OutOptions.RequestedParts.Contains(TEXT("rawgraphpayload")))
    {
        OutOptions.bIncludeRawGraphPayload = true;
    }
}

bool ShouldIncludeMetaPart_BP(const FBlueprintMetaExportOptions& Options, const FString& PartName)
{
    return Options.RequestedParts.Contains(NormalizeMetaPartName_BP(PartName));
}

FString FormatCompactFloat_BP(const double Value)
{
    FString Text = FString::SanitizeFloat(Value);
    if (!Text.Contains(TEXT(".")) && !Text.Contains(TEXT("e")) && !Text.Contains(TEXT("E")))
    {
        Text += TEXT(".0");
    }
    return Text;
}

FString FormatVerboseFloat_BP(const double Value)
{
    return FString::Printf(TEXT("%.6f"), Value);
}

FString QuoteMetaString_BP(const FString& InValue)
{
    FString Escaped = InValue;
    Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return FString::Printf(TEXT("\"%s\""), *Escaped);
}

FString ExportPropertyText_BP(const FProperty* Property, const void* ValuePtr)
{
    FString Text;
    if (Property && ValuePtr)
    {
        Property->ExportTextItem_Direct(Text, ValuePtr, nullptr, nullptr, PPF_None);
    }
    return Text;
}

FString FormatPropertyCppTypeForMeta_BP(const FProperty* Property)
{
    if (!Property)
    {
        return TEXT("value");
    }

    if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
    {
        return BoolProperty->IsNativeBool() ? TEXT("bool") : TEXT("uint8");
    }

    return Property->GetCPPType();
}

FString BuildVariableTerminalTypeText_BP(const FName& Category, const FName& SubCategory, UObject* SubCategoryObject)
{
    if (Category == UEdGraphSchema_K2::PC_Boolean)
    {
        return TEXT("bool");
    }
    if (Category == UEdGraphSchema_K2::PC_Real || Category == UEdGraphSchema_K2::PC_Float || Category == UEdGraphSchema_K2::PC_Double)
    {
        return TEXT("real");
    }
    if (Category == UEdGraphSchema_K2::PC_Int)
    {
        return TEXT("int");
    }
    if (Category == UEdGraphSchema_K2::PC_Int64)
    {
        return TEXT("int64");
    }
    if (Category == UEdGraphSchema_K2::PC_Byte)
    {
        if (const UEnum* Enum = Cast<UEnum>(SubCategoryObject))
        {
            return FString::Printf(TEXT("enum %s"), *Enum->GetName());
        }
        return TEXT("byte");
    }
    if (Category == UEdGraphSchema_K2::PC_Enum)
    {
        if (const UEnum* Enum = Cast<UEnum>(SubCategoryObject))
        {
            return FString::Printf(TEXT("enum %s"), *Enum->GetName());
        }
        return TEXT("enum");
    }
    if (Category == UEdGraphSchema_K2::PC_Name)
    {
        return TEXT("name");
    }
    if (Category == UEdGraphSchema_K2::PC_String)
    {
        return TEXT("string");
    }
    if (Category == UEdGraphSchema_K2::PC_Text)
    {
        return TEXT("text");
    }
    if (Category == UEdGraphSchema_K2::PC_Struct)
    {
        if (const UScriptStruct* Struct = Cast<UScriptStruct>(SubCategoryObject))
        {
            return FString::Printf(TEXT("struct %s"), *Struct->GetName());
        }
        return TEXT("struct");
    }
    if (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_SoftObject || Category == UEdGraphSchema_K2::PC_Interface)
    {
        if (const UClass* Class = Cast<UClass>(SubCategoryObject))
        {
            return FString::Printf(TEXT("object %s"), *Class->GetName());
        }
        return TEXT("object");
    }
    if (Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass)
    {
        if (const UClass* Class = Cast<UClass>(SubCategoryObject))
        {
            return FString::Printf(TEXT("class %s"), *Class->GetName());
        }
        return TEXT("class");
    }

    return Category.ToString();
}

FString BuildVariableTypeText_BP(const FEdGraphPinType& PinType)
{
    FString BaseType = BuildVariableTerminalTypeText_BP(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
    switch (PinType.ContainerType)
    {
    case EPinContainerType::Array:
        return FString::Printf(TEXT("TArray<%s>"), *BaseType);

    case EPinContainerType::Set:
        return FString::Printf(TEXT("TSet<%s>"), *BaseType);

    case EPinContainerType::Map:
    {
        const FString KeyType = BuildVariableTerminalTypeText_BP(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
        return FString::Printf(TEXT("TMap<%s>"), *KeyType);
    }

    default:
        return BaseType;
    }
}

FString BuildSignatureTerminalTypeText_BP(const FName& Category, const FName& SubCategory, UObject* SubCategoryObject)
{
    if (Category == UEdGraphSchema_K2::PC_Boolean)
    {
        return TEXT("bool");
    }
    if (Category == UEdGraphSchema_K2::PC_Float)
    {
        return TEXT("float");
    }
    if (Category == UEdGraphSchema_K2::PC_Double)
    {
        return TEXT("double");
    }
    if (Category == UEdGraphSchema_K2::PC_Real)
    {
        return SubCategory == UEdGraphSchema_K2::PC_Float ? TEXT("float") : TEXT("double");
    }
    if (Category == UEdGraphSchema_K2::PC_Int)
    {
        return TEXT("int32");
    }
    if (Category == UEdGraphSchema_K2::PC_Int64)
    {
        return TEXT("int64");
    }
    if (Category == UEdGraphSchema_K2::PC_Byte || Category == UEdGraphSchema_K2::PC_Enum)
    {
        if (const UEnum* Enum = Cast<UEnum>(SubCategoryObject))
        {
            return FString::Printf(TEXT("TEnumAsByte<%s>"), *Enum->GetName());
        }
        return TEXT("uint8");
    }
    if (Category == UEdGraphSchema_K2::PC_Name)
    {
        return TEXT("name");
    }
    if (Category == UEdGraphSchema_K2::PC_String)
    {
        return TEXT("FString");
    }
    if (Category == UEdGraphSchema_K2::PC_Text)
    {
        return TEXT("FText");
    }
    if (Category == UEdGraphSchema_K2::PC_Struct)
    {
        if (const UScriptStruct* Struct = Cast<UScriptStruct>(SubCategoryObject))
        {
            return FString::Printf(TEXT("F%s"), *Struct->GetName());
        }
        return TEXT("FStruct");
    }
    if (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Interface)
    {
        if (const UClass* Class = Cast<UClass>(SubCategoryObject))
        {
            return FString::Printf(TEXT("%s%s*"), Class->GetPrefixCPP(), *Class->GetName());
        }
        return TEXT("UObject*");
    }
    if (Category == UEdGraphSchema_K2::PC_SoftObject)
    {
        if (const UClass* Class = Cast<UClass>(SubCategoryObject))
        {
            return FString::Printf(TEXT("TSoftObjectPtr<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
        }
        return TEXT("TSoftObjectPtr<UObject>");
    }
    if (Category == UEdGraphSchema_K2::PC_Class)
    {
        if (const UClass* Class = Cast<UClass>(SubCategoryObject))
        {
            return FString::Printf(TEXT("TSubclassOf<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
        }
        return TEXT("TSubclassOf<UObject>");
    }
    if (Category == UEdGraphSchema_K2::PC_SoftClass)
    {
        if (const UClass* Class = Cast<UClass>(SubCategoryObject))
        {
            return FString::Printf(TEXT("TSoftClassPtr<%s%s>"), Class->GetPrefixCPP(), *Class->GetName());
        }
        return TEXT("TSoftClassPtr<UObject>");
    }

    return Category.ToString();
}

FString BuildSignatureTypeText_BP(const FEdGraphPinType& PinType)
{
    FString BaseType = BuildSignatureTerminalTypeText_BP(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
    switch (PinType.ContainerType)
    {
    case EPinContainerType::Array:
        BaseType = FString::Printf(TEXT("TArray<%s>"), *BuildSignatureTerminalTypeText_BP(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get()));
        break;

    case EPinContainerType::Set:
        BaseType = FString::Printf(TEXT("TSet<%s>"), *BuildSignatureTerminalTypeText_BP(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get()));
        break;

    case EPinContainerType::Map:
    {
        const FString KeyType = BuildSignatureTerminalTypeText_BP(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
        const FString ValueType = BuildSignatureTerminalTypeText_BP(
            PinType.PinValueType.TerminalCategory,
            PinType.PinValueType.TerminalSubCategory,
            PinType.PinValueType.TerminalSubCategoryObject.Get());
        BaseType = FString::Printf(TEXT("TMap<%s,%s>"), *KeyType, *ValueType);
        break;
    }

    default:
        break;
    }

    if (PinType.bIsReference)
    {
        BaseType += TEXT("&");
    }

    return BaseType;
}

FString FormatMetaScalarValue_BP(const FProperty* Property, const void* ValuePtr, EBlueprintMetaValueStyle Style)
{
    if (!Property || !ValuePtr)
    {
        return TEXT("None");
    }

    const bool bPreferCompactNumbers = Style != EBlueprintMetaValueStyle::DefaultPropertyVerbose;
    const bool bQuoteTextLikeValues = Style == EBlueprintMetaValueStyle::ContainerElementCompact;

    if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
    {
        return BoolProperty->GetPropertyValue(ValuePtr) ? TEXT("True") : TEXT("False");
    }

    if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
    {
        const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
        if (const UEnum* Enum = EnumProperty->GetEnum())
        {
            return Enum->GetNameStringByValue(EnumValue);
        }
        return LexToString(EnumValue);
    }

    if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
    {
        if (const UEnum* Enum = ByteProperty->GetIntPropertyEnum())
        {
            return Enum->GetNameStringByValue(ByteProperty->GetPropertyValue(ValuePtr));
        }
        return LexToString(ByteProperty->GetPropertyValue(ValuePtr));
    }

    if (const FNumericProperty* NumericProperty = CastField<const FNumericProperty>(Property))
    {
        if (NumericProperty->IsInteger())
        {
            return LexToString(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
        }

        const double NumericValue = NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
        return bPreferCompactNumbers ? FormatCompactFloat_BP(NumericValue) : FormatVerboseFloat_BP(NumericValue);
    }

    if (const FNameProperty* NameProperty = CastField<const FNameProperty>(Property))
    {
        const FString NameText = NameProperty->GetPropertyValue(ValuePtr).ToString();
        return bQuoteTextLikeValues ? QuoteMetaString_BP(NameText) : NameText;
    }

    if (const FStrProperty* StringProperty = CastField<const FStrProperty>(Property))
    {
        const FString StringText = StringProperty->GetPropertyValue(ValuePtr);
        return bQuoteTextLikeValues ? QuoteMetaString_BP(StringText) : StringText;
    }

    if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property))
    {
        const FString TextValue = TextProperty->GetPropertyValue(ValuePtr).ToString();
        return bQuoteTextLikeValues ? QuoteMetaString_BP(TextValue) : TextValue;
    }

    if (const FClassProperty* ClassProperty = CastField<const FClassProperty>(Property))
    {
        UClass* ClassValue = Cast<UClass>(ClassProperty->GetObjectPropertyValue(ValuePtr));
        return ClassValue ? FString::Printf(TEXT("Class %s"), *ClassValue->GetPathName()) : TEXT("None");
    }

    if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
    {
        UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(ValuePtr);
        return ObjectValue ? FString::Printf(TEXT("%s %s"), *ObjectValue->GetClass()->GetName(), *ObjectValue->GetPathName()) : TEXT("None");
    }

    return ExportPropertyText_BP(Property, ValuePtr);
}

FString SummarizeMetaPropertyValue_BP(const FProperty* Property, const void* ValuePtr)
{
    if (!Property || !ValuePtr)
    {
        return TEXT("None");
    }

    if (const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(Property))
    {
        FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
        return FString::Printf(TEXT("[%d elements]"), ArrayHelper.Num());
    }

    if (const FSetProperty* SetProperty = CastField<const FSetProperty>(Property))
    {
        FScriptSetHelper SetHelper(SetProperty, ValuePtr);
        return FString::Printf(TEXT("[%d elements]"), SetHelper.Num());
    }

    if (const FMapProperty* MapProperty = CastField<const FMapProperty>(Property))
    {
        FScriptMapHelper MapHelper(MapProperty, ValuePtr);
        return FString::Printf(TEXT("[%d pairs]"), MapHelper.Num());
    }

    if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
    {
        const FString StructName = StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("Struct");
        return FString::Printf(TEXT("<%s>"), *StructName);
    }

    return FormatMetaScalarValue_BP(Property, ValuePtr, EBlueprintMetaValueStyle::ContainerElementCompact);
}

FString FormatMetaOverflowSuffix_BP(const int32 TotalCount, const int32 EmittedCount, const FString& Noun)
{
    const int32 RemainingCount = FMath::Max(0, TotalCount - EmittedCount);
    if (RemainingCount <= 0)
    {
        return FString();
    }

    return FString::Printf(TEXT("... (+%d more %s)"), RemainingCount, *Noun);
}

FString FormatMetaPropertyValueRecursive_BP(
    const FProperty* Property,
    const void* ValuePtr,
    EBlueprintMetaValueStyle Style,
    const int32 CurrentDepth,
    const int32 MaxDepth,
    const int32 MaxItems)
{
    if (!Property || !ValuePtr)
    {
        return TEXT("None");
    }

    if (CurrentDepth >= MaxDepth &&
        (Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>() || Property->IsA<FStructProperty>()))
    {
        return SummarizeMetaPropertyValue_BP(Property, ValuePtr);
    }

    if (const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(Property))
    {
        FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
        if (ArrayHelper.Num() == 0)
        {
            return TEXT("[]");
        }

        if (Style == EBlueprintMetaValueStyle::DefaultPropertyVerbose)
        {
            TArray<FString> Lines;
            Lines.Add(FString::Printf(TEXT("[%d elements]"), ArrayHelper.Num()));
            const int32 ElementsToEmit = MaxItems <= 0 ? 0 : FMath::Min(ArrayHelper.Num(), MaxItems);
            for (int32 Index = 0; Index < ElementsToEmit; ++Index)
            {
                const FString ElementValue = FormatMetaPropertyValueRecursive_BP(
                    ArrayProperty->Inner,
                    ArrayHelper.GetRawPtr(Index),
                    EBlueprintMetaValueStyle::ContainerElementCompact,
                    CurrentDepth + 1,
                    MaxDepth,
                    MaxItems);
                Lines.Add(FString::Printf(TEXT("    [%d] %s"), Index, *ElementValue));
            }

            const FString OverflowText = FormatMetaOverflowSuffix_BP(ArrayHelper.Num(), ElementsToEmit, TEXT("elements"));
            if (!OverflowText.IsEmpty())
            {
                Lines.Add(FString::Printf(TEXT("    %s"), *OverflowText));
            }
            return FString::Join(Lines, TEXT("\n"));
        }

        TArray<FString> Elements;
        const int32 ElementsToEmit = MaxItems <= 0 ? 0 : FMath::Min(ArrayHelper.Num(), MaxItems);
        Elements.Reserve(ElementsToEmit + 1);
        for (int32 Index = 0; Index < ElementsToEmit; ++Index)
        {
            Elements.Add(FormatMetaPropertyValueRecursive_BP(
                ArrayProperty->Inner,
                ArrayHelper.GetRawPtr(Index),
                EBlueprintMetaValueStyle::ContainerElementCompact,
                CurrentDepth + 1,
                MaxDepth,
                MaxItems));
        }

        const FString OverflowText = FormatMetaOverflowSuffix_BP(ArrayHelper.Num(), ElementsToEmit, TEXT("elements"));
        if (!OverflowText.IsEmpty())
        {
            Elements.Add(OverflowText);
        }

        return FString::Printf(TEXT("[%s]"), *FString::Join(Elements, TEXT(", ")));
    }

    if (const FSetProperty* SetProperty = CastField<const FSetProperty>(Property))
    {
        FScriptSetHelper SetHelper(SetProperty, ValuePtr);
        if (SetHelper.Num() == 0)
        {
            return TEXT("[]");
        }

        TArray<FString> Elements;
        const int32 ElementsToEmit = MaxItems <= 0 ? 0 : FMath::Min(SetHelper.Num(), MaxItems);
        int32 EmittedCount = 0;
        for (int32 Index = 0; Index < SetHelper.GetMaxIndex(); ++Index)
        {
            if (!SetHelper.IsValidIndex(Index) || EmittedCount >= ElementsToEmit)
            {
                continue;
            }

            Elements.Add(FormatMetaPropertyValueRecursive_BP(
                SetProperty->ElementProp,
                SetHelper.GetElementPtr(Index),
                EBlueprintMetaValueStyle::ContainerElementCompact,
                CurrentDepth + 1,
                MaxDepth,
                MaxItems));
            ++EmittedCount;
        }

        const FString OverflowText = FormatMetaOverflowSuffix_BP(SetHelper.Num(), EmittedCount, TEXT("elements"));
        if (!OverflowText.IsEmpty())
        {
            Elements.Add(OverflowText);
        }

        return FString::Printf(TEXT("[%s]"), *FString::Join(Elements, TEXT(", ")));
    }

    if (const FMapProperty* MapProperty = CastField<const FMapProperty>(Property))
    {
        FScriptMapHelper MapHelper(MapProperty, ValuePtr);
        if (MapHelper.Num() == 0)
        {
            return TEXT("[]");
        }

        TArray<FString> Lines;
        const int32 PairsToEmit = MaxItems <= 0 ? 0 : FMath::Min(MapHelper.Num(), MaxItems);
        int32 EmittedPairs = 0;
        for (int32 Index = 0; Index < MapHelper.GetMaxIndex(); ++Index)
        {
            if (!MapHelper.IsValidIndex(Index) || EmittedPairs >= PairsToEmit)
            {
                continue;
            }

            const FString KeyText = FormatMetaPropertyValueRecursive_BP(
                MapProperty->KeyProp,
                MapHelper.GetKeyPtr(Index),
                EBlueprintMetaValueStyle::ContainerElementCompact,
                CurrentDepth + 1,
                MaxDepth,
                MaxItems);
            const FString ValueText = FormatMetaPropertyValueRecursive_BP(
                MapProperty->ValueProp,
                MapHelper.GetValuePtr(Index),
                EBlueprintMetaValueStyle::ContainerElementCompact,
                CurrentDepth + 1,
                MaxDepth,
                MaxItems);
            Lines.Add(FString::Printf(TEXT("[%s] %s"), *KeyText, *ValueText));
            ++EmittedPairs;
        }

        const FString OverflowText = FormatMetaOverflowSuffix_BP(MapHelper.Num(), EmittedPairs, TEXT("pairs"));
        if (!OverflowText.IsEmpty())
        {
            if (Style == EBlueprintMetaValueStyle::DefaultPropertyVerbose)
            {
                Lines.Add(FString::Printf(TEXT("    %s"), *OverflowText));
            }
            else
            {
                Lines.Add(OverflowText);
            }
        }

        return FString::Join(Lines, TEXT("\n"));
    }

    if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
    {
        const FString StructName = StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("Struct");
        if (!StructProperty->Struct)
        {
            return FString::Printf(TEXT("<%s>"), *StructName);
        }

        TArray<FString> FieldEntries;
        const int32 FieldsToEmit = MaxItems <= 0 ? 0 : MaxItems;
        int32 EmittedFields = 0;
        for (TFieldIterator<FProperty> It(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* ChildProperty = *It;
            if (!ChildProperty || ChildProperty->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || EmittedFields >= FieldsToEmit)
            {
                continue;
            }

            const void* ChildValuePtr = ChildProperty->ContainerPtrToValuePtr<void>(ValuePtr);
            const FString ChildValue = FormatMetaPropertyValueRecursive_BP(
                ChildProperty,
                ChildValuePtr,
                EBlueprintMetaValueStyle::ContainerElementCompact,
                CurrentDepth + 1,
                MaxDepth,
                MaxItems);
            FieldEntries.Add(FString::Printf(TEXT("%s=%s"), *ChildProperty->GetName(), *ChildValue));
            ++EmittedFields;
        }

        int32 TotalFieldCount = 0;
        for (TFieldIterator<FProperty> It(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            const FProperty* ChildProperty = *It;
            if (ChildProperty && !ChildProperty->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
            {
                ++TotalFieldCount;
            }
        }

        if (Style == EBlueprintMetaValueStyle::DefaultPropertyVerbose)
        {
            if (FieldEntries.Num() == 0)
            {
                return FString::Printf(TEXT("<%s>"), *StructName);
            }

            TArray<FString> Lines;
            Lines.Add(FString::Printf(TEXT("<%s>"), *StructName));
            for (const FString& FieldEntry : FieldEntries)
            {
                Lines.Add(FString::Printf(TEXT("    %s"), *FieldEntry));
            }
            const FString OverflowText = FormatMetaOverflowSuffix_BP(TotalFieldCount, EmittedFields, TEXT("fields"));
            if (!OverflowText.IsEmpty())
            {
                Lines.Add(FString::Printf(TEXT("    %s"), *OverflowText));
            }
            return FString::Join(Lines, TEXT("\n"));
        }

        const FString OverflowText = FormatMetaOverflowSuffix_BP(TotalFieldCount, EmittedFields, TEXT("fields"));
        if (!OverflowText.IsEmpty())
        {
            FieldEntries.Add(OverflowText);
        }

        return FString::Printf(TEXT("%s(%s)"), *StructName, *FString::Join(FieldEntries, TEXT(", ")));
    }

    return FormatMetaScalarValue_BP(Property, ValuePtr, Style);
}

FString FormatMetaPropertyValue_BP(const FProperty* Property, const void* ValuePtr, EBlueprintMetaValueStyle Style, const int32 MaxDepth, const int32 MaxItems)
{
    return FormatMetaPropertyValueRecursive_BP(
        Property,
        ValuePtr,
        Style,
        0,
        FMath::Max(0, MaxDepth),
        FMath::Max(0, MaxItems));
}

bool TryGetJsonValueAsString_BP(const TSharedPtr<FJsonValue>& Value, FString& OutString)
{
    if (!Value.IsValid())
    {
        return false;
    }

    switch (Value->Type)
    {
    case EJson::String:
        OutString = Value->AsString();
        return true;

    case EJson::Number:
        OutString = FString::SanitizeFloat(Value->AsNumber());
        return true;

    case EJson::Boolean:
        OutString = Value->AsBool() ? TEXT("true") : TEXT("false");
        return true;

    case EJson::Null:
        OutString = TEXT("");
        return true;

    default:
        return false;
    }
}

bool TryGetJsonValueAsBool_BP(const TSharedPtr<FJsonValue>& Value, bool& OutBool)
{
    if (!Value.IsValid())
    {
        return false;
    }

    if (Value->Type == EJson::Boolean)
    {
        OutBool = Value->AsBool();
        return true;
    }

    if (Value->Type == EJson::Number)
    {
        OutBool = Value->AsNumber() != 0.0;
        return true;
    }

    if (Value->Type == EJson::String)
    {
        FString TextValue = Value->AsString();
        TextValue.TrimStartAndEndInline();
        TextValue.ToLowerInline();

        if (TextValue == TEXT("true") || TextValue == TEXT("1") || TextValue == TEXT("on") || TextValue == TEXT("enable") || TextValue == TEXT("enabled"))
        {
            OutBool = true;
            return true;
        }

        if (TextValue == TEXT("false") || TextValue == TEXT("0") || TextValue == TEXT("off") || TextValue == TEXT("disable") || TextValue == TEXT("disabled"))
        {
            OutBool = false;
            return true;
        }
    }

    return false;
}

bool TryGetJsonValueAsNumber_BP(const TSharedPtr<FJsonValue>& Value, double& OutNumber)
{
    if (!Value.IsValid())
    {
        return false;
    }

    if (Value->Type == EJson::Number)
    {
        OutNumber = Value->AsNumber();
        return true;
    }

    if (Value->Type == EJson::String)
    {
        double ParsedNumber = 0.0;
        if (FDefaultValueHelper::ParseDouble(Value->AsString(), ParsedNumber))
        {
            OutNumber = ParsedNumber;
            return true;
        }
    }

    return false;
}

bool TryGetNumericFieldFromJsonObject_BP(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, double& OutNumber)
{
    if (!Object.IsValid())
    {
        return false;
    }

    return Object->TryGetNumberField(FieldName, OutNumber);
}

bool TryConvertJsonValueToImportText_BP(const TSharedPtr<FJsonValue>& Value, FString& OutImportText)
{
    if (!Value.IsValid())
    {
        return false;
    }

    switch (Value->Type)
    {
    case EJson::String:
        OutImportText = Value->AsString();
        return true;

    case EJson::Number:
        OutImportText = FString::SanitizeFloat(Value->AsNumber());
        return true;

    case EJson::Boolean:
        OutImportText = Value->AsBool() ? TEXT("true") : TEXT("false");
        return true;

    case EJson::Null:
        OutImportText = TEXT("");
        return true;

    case EJson::Array:
    {
        const TArray<TSharedPtr<FJsonValue>>& JsonArray = Value->AsArray();
        if (JsonArray.Num() == 3)
        {
            double X = 0.0;
            double Y = 0.0;
            double Z = 0.0;
            if (TryGetJsonValueAsNumber_BP(JsonArray[0], X) &&
                TryGetJsonValueAsNumber_BP(JsonArray[1], Y) &&
                TryGetJsonValueAsNumber_BP(JsonArray[2], Z))
            {
                OutImportText = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
                    *FString::SanitizeFloat(X),
                    *FString::SanitizeFloat(Y),
                    *FString::SanitizeFloat(Z));
                return true;
            }
        }

        return false;
    }

    case EJson::Object:
    {
        const TSharedPtr<FJsonObject> JsonObject = Value->AsObject();
        if (!JsonObject.IsValid())
        {
            return false;
        }

        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        if (TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("X"), X) &&
            TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("Y"), Y) &&
            TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("Z"), Z))
        {
            OutImportText = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
                *FString::SanitizeFloat(X),
                *FString::SanitizeFloat(Y),
                *FString::SanitizeFloat(Z));
            return true;
        }

        double Pitch = 0.0;
        double Yaw = 0.0;
        double Roll = 0.0;
        if (TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("Pitch"), Pitch) &&
            TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("Yaw"), Yaw) &&
            TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("Roll"), Roll))
        {
            OutImportText = FString::Printf(TEXT("(Pitch=%s,Yaw=%s,Roll=%s)"),
                *FString::SanitizeFloat(Pitch),
                *FString::SanitizeFloat(Yaw),
                *FString::SanitizeFloat(Roll));
            return true;
        }

        double R = 0.0;
        double G = 0.0;
        double B = 0.0;
        double A = 1.0;
        if (TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("R"), R) &&
            TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("G"), G) &&
            TryGetNumericFieldFromJsonObject_BP(JsonObject, TEXT("B"), B))
        {
            JsonObject->TryGetNumberField(TEXT("A"), A);
            OutImportText = FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"),
                *FString::SanitizeFloat(R),
                *FString::SanitizeFloat(G),
                *FString::SanitizeFloat(B),
                *FString::SanitizeFloat(A));
            return true;
        }

        return false;
    }

    default:
        return false;
    }
}

UBlueprint* ResolveBlueprintFromParams_BP(const TSharedPtr<FJsonObject>& Params, FString& OutBlueprintRef)
{
    OutBlueprintRef = ResolveBlueprintTargetParam(Params);
    if (OutBlueprintRef.IsEmpty())
    {
        return nullptr;
    }

    return FEpicUnrealMCPCommonUtils::FindBlueprint(OutBlueprintRef);
}

void AddMetadataIfPresent_BP(const FField* Field, const TSharedPtr<FJsonObject>& MetadataObject, const TCHAR* Key)
{
    if (!Field || !MetadataObject.IsValid() || !Field->HasMetaData(Key))
    {
        return;
    }

    MetadataObject->SetStringField(Key, Field->GetMetaData(Key));
}

TSharedPtr<FJsonObject> BuildPropertySpecifierJson_BP(const FProperty* Property)
{
    TSharedPtr<FJsonObject> Specifiers = MakeShared<FJsonObject>();
    if (!Property)
    {
        return Specifiers;
    }

    Specifiers->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
    Specifiers->SetBoolField(TEXT("blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
    Specifiers->SetBoolField(TEXT("blueprint_read_only"), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
    Specifiers->SetBoolField(TEXT("disable_edit_on_instance"), Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
    Specifiers->SetBoolField(TEXT("config"), Property->HasAnyPropertyFlags(CPF_Config));
    Specifiers->SetBoolField(TEXT("transient"), Property->HasAnyPropertyFlags(CPF_Transient));
    Specifiers->SetBoolField(TEXT("interp"), Property->HasAnyPropertyFlags(CPF_Interp));
    Specifiers->SetBoolField(TEXT("replicated"), Property->HasAnyPropertyFlags(CPF_Net));
    Specifiers->SetBoolField(TEXT("save_game"), Property->HasAnyPropertyFlags(CPF_SaveGame));
    Specifiers->SetBoolField(TEXT("advanced_display"), Property->HasAnyPropertyFlags(CPF_AdvancedDisplay));
    return Specifiers;
}

TSharedPtr<FJsonObject> BuildVariableSpecifierJson_BP(const FBPVariableDescription& Variable)
{
    TSharedPtr<FJsonObject> Specifiers = MakeShared<FJsonObject>();
    Specifiers->SetBoolField(TEXT("is_public"), (Variable.PropertyFlags & CPF_Edit) != 0);
    Specifiers->SetBoolField(TEXT("is_blueprint_visible"), (Variable.PropertyFlags & CPF_BlueprintVisible) != 0);
    Specifiers->SetBoolField(TEXT("is_blueprint_writable"), (Variable.PropertyFlags & CPF_BlueprintReadOnly) == 0);
    Specifiers->SetBoolField(TEXT("is_editable_in_instance"), (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0);
    Specifiers->SetBoolField(TEXT("is_config"), (Variable.PropertyFlags & CPF_Config) != 0);
    Specifiers->SetBoolField(TEXT("replication_enabled"), (Variable.PropertyFlags & CPF_Net) != 0);
    Specifiers->SetBoolField(TEXT("expose_to_cinematics"), (Variable.PropertyFlags & CPF_Interp) != 0);
    Specifiers->SetBoolField(TEXT("is_private"), Variable.HasMetaData(TEXT("AllowPrivateAccess")));
    Specifiers->SetBoolField(TEXT("expose_on_spawn"), Variable.HasMetaData(TEXT("ExposeOnSpawn")));
    Specifiers->SetBoolField(TEXT("bitmask"), Variable.HasMetaData(TEXT("Bitmask")));
    return Specifiers;
}

FString DescribePropertyValueKind_BP(const FProperty* Property)
{
    if (!Property)
    {
        return TEXT("unknown");
    }

    if (Property->IsA<FBoolProperty>())
    {
        return TEXT("bool");
    }
    if (const FNumericProperty* NumericProperty = CastField<const FNumericProperty>(Property))
    {
        return NumericProperty->IsInteger() ? TEXT("integer") : TEXT("number");
    }
    if (Property->IsA<FStrProperty>())
    {
        return TEXT("string");
    }
    if (Property->IsA<FNameProperty>())
    {
        return TEXT("name");
    }
    if (Property->IsA<FTextProperty>())
    {
        return TEXT("text");
    }
    if (Property->IsA<FEnumProperty>() || Property->IsA<FByteProperty>())
    {
        return TEXT("enum");
    }
    if (Property->IsA<FClassProperty>())
    {
        return TEXT("class");
    }
    if (Property->IsA<FObjectPropertyBase>())
    {
        return TEXT("object");
    }
    if (Property->IsA<FStructProperty>())
    {
        return TEXT("struct");
    }
    if (Property->IsA<FArrayProperty>())
    {
        return TEXT("array");
    }
    if (Property->IsA<FMapProperty>())
    {
        return TEXT("map");
    }
    if (Property->IsA<FSetProperty>())
    {
        return TEXT("set");
    }

    return TEXT("value");
}

TSharedPtr<FJsonValue> BuildPropertyValueJson_BP(const FProperty* Property, const void* ValuePtr, FString& OutValueText)
{
    OutValueText.Reset();
    if (!Property || !ValuePtr)
    {
        return MakeShared<FJsonValueNull>();
    }

    Property->ExportTextItem_Direct(OutValueText, ValuePtr, nullptr, nullptr, PPF_None);

    if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
    {
        return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(ValuePtr));
    }

    if (const FNumericProperty* NumericProperty = CastField<const FNumericProperty>(Property))
    {
        if (NumericProperty->IsInteger())
        {
            return MakeShared<FJsonValueNumber>(static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr)));
        }

        return MakeShared<FJsonValueNumber>(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
    }

    if (const FStrProperty* StringProperty = CastField<const FStrProperty>(Property))
    {
        return MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(ValuePtr));
    }

    if (const FNameProperty* NameProperty = CastField<const FNameProperty>(Property))
    {
        return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(ValuePtr).ToString());
    }

    if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property))
    {
        return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(ValuePtr).ToString());
    }

    if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
    {
        const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
        if (UEnum* EnumDefinition = EnumProperty->GetEnum())
        {
            return MakeShared<FJsonValueString>(EnumDefinition->GetNameStringByValue(EnumValue));
        }
    }

    if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
    {
        if (UEnum* EnumDefinition = ByteProperty->GetIntPropertyEnum())
        {
            return MakeShared<FJsonValueString>(EnumDefinition->GetNameStringByValue(ByteProperty->GetPropertyValue(ValuePtr)));
        }

        return MakeShared<FJsonValueNumber>(static_cast<double>(ByteProperty->GetPropertyValue(ValuePtr)));
    }

    if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
    {
        UObject* PropertyObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
        if (PropertyObject)
        {
            return MakeShareable(new FJsonValueString(PropertyObject->GetPathName()));
        }

        return MakeShareable(new FJsonValueNull());
    }

    return MakeShared<FJsonValueString>(OutValueText);
}

bool BuildPropertyQueryOptionsFromParams_BP(const TSharedPtr<FJsonObject>& Params, FBlueprintPropertyQueryOptions& OutOptions)
{
    if (!Params.IsValid())
    {
        return false;
    }

    Params->TryGetBoolField(TEXT("include_values"), OutOptions.bIncludeValues);
    Params->TryGetBoolField(TEXT("include_metadata"), OutOptions.bIncludeMetadata);
    Params->TryGetBoolField(TEXT("include_inherited"), OutOptions.bIncludeInherited);
    Params->TryGetBoolField(TEXT("editable_only"), OutOptions.bEditableOnly);

    FString PropertyName;
    if (Params->TryGetStringField(TEXT("property_name"), PropertyName) && !PropertyName.IsEmpty())
    {
        OutOptions.RequestedPropertyNames.Add(NormalizeLookupKey(PropertyName));
    }

    const TArray<TSharedPtr<FJsonValue>>* PropertyNames = nullptr;
    if (Params->TryGetArrayField(TEXT("property_names"), PropertyNames))
    {
        for (const TSharedPtr<FJsonValue>& Value : *PropertyNames)
        {
            FString RequestedName;
            if (TryGetJsonValueAsString_BP(Value, RequestedName) && !RequestedName.IsEmpty())
            {
                OutOptions.RequestedPropertyNames.Add(NormalizeLookupKey(RequestedName));
            }
        }
    }

    return true;
}

bool ShouldIncludeProperty_BP(const FProperty* Property, UObject* OwnerObject, const FBlueprintPropertyQueryOptions& Options)
{
    if (!Property || !OwnerObject)
    {
        return false;
    }

    const FString PropertyNameKey = NormalizeLookupKey(Property->GetName());
    const bool bRequestedProperty = Options.RequestedPropertyNames.Contains(PropertyNameKey);
    if (!bRequestedProperty && Options.RequestedPropertyNames.Num() > 0)
    {
        return false;
    }

    if (!Options.bIncludeInherited && Property->GetOwnerStruct() != OwnerObject->GetClass())
    {
        return false;
    }

    if (!bRequestedProperty && Options.bEditableOnly && !Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
    {
        return false;
    }

    return true;
}

TSharedPtr<FJsonObject> DescribeProperty_BP(const FProperty* Property, UObject* OwnerObject, const void* ValuePtr, const FBlueprintPropertyQueryOptions& Options)
{
    TSharedPtr<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
    PropertyObject->SetStringField(TEXT("name"), Property->GetName());
    PropertyObject->SetStringField(TEXT("display_name"), Property->HasMetaData(TEXT("DisplayName")) ? Property->GetMetaData(TEXT("DisplayName")) : Property->GetName());
    PropertyObject->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
    PropertyObject->SetStringField(TEXT("property_class"), Property->GetClass()->GetName());
    PropertyObject->SetStringField(TEXT("owner_struct"), Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetName() : FString());
    PropertyObject->SetBoolField(TEXT("declared_on_target_class"), Property->GetOwnerStruct() == OwnerObject->GetClass());
    PropertyObject->SetStringField(TEXT("value_kind"), DescribePropertyValueKind_BP(Property));
    PropertyObject->SetObjectField(TEXT("specifiers"), BuildPropertySpecifierJson_BP(Property));

    if (Options.bIncludeMetadata)
    {
        TSharedPtr<FJsonObject> MetadataObject = MakeShared<FJsonObject>();
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("Category"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("DisplayName"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("Tooltip"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("ToolTip"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("ClampMin"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("ClampMax"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("UIMin"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("UIMax"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("Units"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("ExposeOnSpawn"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("AllowPrivateAccess"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("Bitmask"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("BitmaskEnum"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("MultiLine"));
        AddMetadataIfPresent_BP(Property, MetadataObject, TEXT("MakeEditWidget"));
        PropertyObject->SetObjectField(TEXT("metadata"), MetadataObject);
    }

    if (Options.bIncludeValues)
    {
        FString ValueText;
        PropertyObject->SetField(TEXT("value"), BuildPropertyValueJson_BP(Property, ValuePtr, ValueText));
        PropertyObject->SetStringField(TEXT("value_text"), ValueText);
    }

    return PropertyObject;
}

void CollectObjectProperties_BP(UObject* TargetObject, const FBlueprintPropertyQueryOptions& Options, TArray<TSharedPtr<FJsonValue>>& OutProperties, bool& bMatchedRequestedProperty)
{
    OutProperties.Reset();
    bMatchedRequestedProperty = Options.RequestedPropertyNames.Num() == 0;

    if (!TargetObject)
    {
        return;
    }

    for (TFieldIterator<FProperty> It(TargetObject->GetClass(), Options.bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (!ShouldIncludeProperty_BP(Property, TargetObject, Options))
        {
            continue;
        }

        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
        OutProperties.Add(MakeShared<FJsonValueObject>(DescribeProperty_BP(Property, TargetObject, ValuePtr, Options)));
        bMatchedRequestedProperty = true;
    }
}

USCS_Node* FindBlueprintComponentNodeByName_BP(UBlueprint* Blueprint, const FString& ComponentName)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript || ComponentName.IsEmpty())
    {
        return nullptr;
    }

    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (!Node || !Node->ComponentTemplate)
        {
            continue;
        }

        const bool bVariableMatch = Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase);
        const bool bNodeMatch = Node->GetName().Equals(ComponentName, ESearchCase::IgnoreCase);
        const bool bObjectMatch = Node->ComponentTemplate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase);
        if (bVariableMatch || bNodeMatch || bObjectMatch)
        {
            return Node;
        }
    }

    return nullptr;
}

void CollectBlueprintComponents_BP(UBlueprint* Blueprint, TArray<FBlueprintComponentHandle>& OutComponents)
{
    OutComponents.Reset();
    if (!Blueprint)
    {
        return;
    }

    TSet<FString> SeenComponentNames;
    auto AddComponent = [&OutComponents, &SeenComponentNames](const FString& Name, UObject* Object, const FString& Source, const bool bIsRoot, const bool bIsInherited, USCS_Node* SCSNode)
    {
        if (Name.IsEmpty() || !Object)
        {
            return;
        }

        const FString LookupKey = NormalizeLookupKey(Name);
        if (SeenComponentNames.Contains(LookupKey))
        {
            return;
        }

        SeenComponentNames.Add(LookupKey);

        FBlueprintComponentHandle ComponentHandle;
        ComponentHandle.Name = Name;
        ComponentHandle.Source = Source;
        ComponentHandle.bIsRoot = bIsRoot;
        ComponentHandle.bIsInherited = bIsInherited;
        ComponentHandle.SCSNode = SCSNode;
        ComponentHandle.Object = Object;
        OutComponents.Add(ComponentHandle);
    };

    if (Blueprint->SimpleConstructionScript)
    {
        USCS_Node* RootNode = Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode();
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node || !Node->ComponentTemplate)
            {
                continue;
            }

            AddComponent(Node->GetVariableName().ToString(), Node->ComponentTemplate, TEXT("scs"), Node == RootNode, false, Node);
        }
    }

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(Blueprint);
    AActor* ActorCDO = BlueprintClass ? Cast<AActor>(BlueprintClass->GetDefaultObject()) : nullptr;
    if (ActorCDO)
    {
        TArray<UActorComponent*> Components;
        ActorCDO->GetComponents(Components);
        for (UActorComponent* Component : Components)
        {
            if (!Component)
            {
                continue;
            }

            AddComponent(Component->GetName(), Component, TEXT("cdo_component"), false, true, nullptr);
        }
    }

    UObject* BlueprintCDO = BlueprintClass ? BlueprintClass->GetDefaultObject() : nullptr;
    if (BlueprintCDO)
    {
        for (TFieldIterator<FObjectPropertyBase> It(BlueprintCDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            FObjectPropertyBase* ObjectProperty = *It;
            if (!ObjectProperty || !ObjectProperty->PropertyClass || !ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
            {
                continue;
            }

            if (UObject* PropertyObject = ObjectProperty->GetObjectPropertyValue_InContainer(BlueprintCDO))
            {
                AddComponent(ObjectProperty->GetName(), PropertyObject, TEXT("cdo_property"), false, true, nullptr);
            }
        }
    }
}

bool SetObjectPropertyLoose_BP(UObject* TargetObject, const FString& RequestedPropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutResolvedPropertyName, FString& OutError)
{
    if (!TargetObject)
    {
        OutError = TEXT("Invalid target object");
        return false;
    }

    FProperty* Property = FindPropertyByNameLoose(TargetObject->GetClass(), RequestedPropertyName);
    if (!Property)
    {
        OutError = FString::Printf(TEXT("Property not found: %s"), *RequestedPropertyName);
        return false;
    }

    OutResolvedPropertyName = Property->GetName();
    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
    if (!ValuePtr)
    {
        OutError = FString::Printf(TEXT("Failed to resolve storage for property: %s"), *OutResolvedPropertyName);
        return false;
    }

    TargetObject->Modify();

    if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        bool BoolValue = false;
        if (!TryGetJsonValueAsBool_BP(Value, BoolValue))
        {
            OutError = FString::Printf(TEXT("Property '%s' expects a boolean value"), *OutResolvedPropertyName);
            return false;
        }

        BoolProperty->SetPropertyValue(ValuePtr, BoolValue);
        return true;
    }

    if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        double NumericValue = 0.0;
        if (!TryGetJsonValueAsNumber_BP(Value, NumericValue))
        {
            OutError = FString::Printf(TEXT("Property '%s' expects a numeric value"), *OutResolvedPropertyName);
            return false;
        }

        if (NumericProperty->IsInteger())
        {
            NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(FMath::RoundToInt64(NumericValue)));
        }
        else
        {
            NumericProperty->SetFloatingPointPropertyValue(ValuePtr, NumericValue);
        }

        return true;
    }

    if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
    {
        FString TextValue;
        if (!TryGetJsonValueAsString_BP(Value, TextValue))
        {
            OutError = FString::Printf(TEXT("Property '%s' expects a string-compatible value"), *OutResolvedPropertyName);
            return false;
        }

        StringProperty->SetPropertyValue(ValuePtr, TextValue);
        return true;
    }

    if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        FString TextValue;
        if (!TryGetJsonValueAsString_BP(Value, TextValue))
        {
            OutError = FString::Printf(TEXT("Property '%s' expects a string-compatible value"), *OutResolvedPropertyName);
            return false;
        }

        NameProperty->SetPropertyValue(ValuePtr, FName(*TextValue));
        return true;
    }

    if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
    {
        FString TextValue;
        if (!TryGetJsonValueAsString_BP(Value, TextValue))
        {
            OutError = FString::Printf(TEXT("Property '%s' expects a string-compatible value"), *OutResolvedPropertyName);
            return false;
        }

        TextProperty->SetPropertyValue(ValuePtr, FText::FromString(TextValue));
        return true;
    }

    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        if (Value->Type == EJson::Number)
        {
            EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value->AsNumber()));
            return true;
        }

        FString EnumValueText;
        if (!TryGetJsonValueAsString_BP(Value, EnumValueText))
        {
            OutError = FString::Printf(TEXT("Property '%s' expects an enum name or numeric value"), *OutResolvedPropertyName);
            return false;
        }

        UEnum* EnumDefinition = EnumProperty->GetEnum();
        const int64 EnumValue = EnumDefinition ? EnumDefinition->GetValueByNameString(EnumValueText) : INDEX_NONE;
        if (!EnumDefinition || EnumValue == INDEX_NONE)
        {
            OutError = FString::Printf(TEXT("Unknown enum value '%s' for property '%s'"), *EnumValueText, *OutResolvedPropertyName);
            return false;
        }

        EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
        return true;
    }

    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        if (Value->Type == EJson::Number)
        {
            ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(Value->AsNumber()));
            return true;
        }

        if (UEnum* EnumDefinition = ByteProperty->GetIntPropertyEnum())
        {
            FString EnumValueText;
            if (!TryGetJsonValueAsString_BP(Value, EnumValueText))
            {
                OutError = FString::Printf(TEXT("Property '%s' expects an enum name or numeric value"), *OutResolvedPropertyName);
                return false;
            }

            const int64 EnumValue = EnumDefinition->GetValueByNameString(EnumValueText);
            if (EnumValue == INDEX_NONE)
            {
                OutError = FString::Printf(TEXT("Unknown enum value '%s' for property '%s'"), *EnumValueText, *OutResolvedPropertyName);
                return false;
            }

            ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(EnumValue));
            return true;
        }
    }

    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        FString ObjectReference;
        if (!TryGetJsonValueAsString_BP(Value, ObjectReference))
        {
            OutError = FString::Printf(TEXT("Property '%s' expects an object reference string"), *OutResolvedPropertyName);
            return false;
        }

        UObject* ResolvedObject = ResolveObjectByReference(ObjectReference);
        if (!ResolvedObject && !ObjectReference.IsEmpty())
        {
            ResolvedObject = UEditorAssetLibrary::LoadAsset(ObjectReference);
        }

        if (ResolvedObject && !ResolvedObject->IsA(ObjectProperty->PropertyClass))
        {
            OutError = FString::Printf(TEXT("Resolved object for '%s' is not a %s"), *OutResolvedPropertyName, *ObjectProperty->PropertyClass->GetName());
            return false;
        }

        ObjectProperty->SetObjectPropertyValue(ValuePtr, ResolvedObject);
        return true;
    }

    FString ImportText;
    if (!TryConvertJsonValueToImportText_BP(Value, ImportText))
    {
        OutError = FString::Printf(TEXT("Unsupported JSON value for property '%s'"), *OutResolvedPropertyName);
        return false;
    }

    if (!Property->ImportText_Direct(*ImportText, ValuePtr, TargetObject, PPF_None))
    {
        OutError = FString::Printf(TEXT("Failed to import value '%s' for property '%s'"), *ImportText, *OutResolvedPropertyName);
        return false;
    }

    return true;
}

bool ApplySceneComponentOverrides_BP(USceneComponent* SceneComponent, const TSharedPtr<FJsonObject>& Params, TArray<TSharedPtr<FJsonValue>>& OutAppliedFields)
{
    if (!SceneComponent || !Params.IsValid())
    {
        return false;
    }

    bool bApplied = false;

    if (Params->HasField(TEXT("location")))
    {
        SceneComponent->SetRelativeLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
        OutAppliedFields.Add(MakeShared<FJsonValueString>(TEXT("location")));
        bApplied = true;
    }

    if (Params->HasField(TEXT("rotation")))
    {
        SceneComponent->SetRelativeRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
        OutAppliedFields.Add(MakeShared<FJsonValueString>(TEXT("rotation")));
        bApplied = true;
    }

    if (Params->HasField(TEXT("scale")))
    {
        SceneComponent->SetRelativeScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
        OutAppliedFields.Add(MakeShared<FJsonValueString>(TEXT("scale")));
        bApplied = true;
    }

    return bApplied;
}

bool ApplyPropertyMapToObject_BP(UObject* TargetObject, const TSharedPtr<FJsonObject>& PropertyMap, TArray<TSharedPtr<FJsonValue>>& OutAppliedProperties, FString& OutError)
{
    OutAppliedProperties.Reset();
    if (!TargetObject || !PropertyMap.IsValid())
    {
        return true;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : PropertyMap->Values)
    {
        FString ResolvedPropertyName;
        FString PropertyError;
        if (!SetObjectPropertyLoose_BP(TargetObject, Entry.Key, Entry.Value, ResolvedPropertyName, PropertyError))
        {
            OutError = PropertyError;
            return false;
        }

        TSharedPtr<FJsonObject> AppliedProperty = MakeShared<FJsonObject>();
        AppliedProperty->SetStringField(TEXT("requested_name"), Entry.Key);
        AppliedProperty->SetStringField(TEXT("resolved_name"), ResolvedPropertyName);
        OutAppliedProperties.Add(MakeShared<FJsonValueObject>(AppliedProperty));
    }

    return true;
}

bool BlueprintHasVariable_BP(const UBlueprint* Blueprint, const FString& VariableName)
{
    if (!Blueprint || VariableName.IsEmpty())
    {
        return false;
    }

    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarName.ToString().Equals(VariableName, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    return false;
}

FString GetMetaPinDisplayName_BP(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return FString();
    }

    if (const UEdGraphSchema* Schema = Pin->GetSchema())
    {
        const FString DisplayName = Schema->GetPinDisplayName(Pin).ToString();
        if (!DisplayName.IsEmpty())
        {
            return DisplayName;
        }
    }

    return Pin->PinName.ToString();
}

FString FormatPinDefaultValueForMeta_BP(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return TEXT("None");
    }

    if (Pin->DefaultObject)
    {
        return FString::Printf(TEXT("%s %s"), *Pin->DefaultObject->GetClass()->GetName(), *Pin->DefaultObject->GetPathName());
    }

    FString DefaultText = Pin->GetDefaultAsString();
    if (DefaultText.IsEmpty() && !Pin->DefaultTextValue.IsEmpty())
    {
        DefaultText = Pin->DefaultTextValue.ToString();
    }

    if (DefaultText.IsEmpty())
    {
        if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface ||
            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
        {
            return TEXT("None");
        }
    }

    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
    {
        if (DefaultText.Equals(TEXT("true"), ESearchCase::IgnoreCase))
        {
            return TEXT("True");
        }
        if (DefaultText.Equals(TEXT("false"), ESearchCase::IgnoreCase))
        {
            return TEXT("False");
        }
    }

    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Float ||
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Double ||
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
    {
        double NumericValue = 0.0;
        if (FDefaultValueHelper::ParseDouble(DefaultText, NumericValue))
        {
            return FormatVerboseFloat_BP(NumericValue);
        }
    }

    if (DefaultText.IsEmpty())
    {
        return TEXT("None");
    }

    return DefaultText;
}

FBlueprintMetaParamInfo BuildMetaParamInfoFromPin_BP(const UEdGraphPin* Pin)
{
    FBlueprintMetaParamInfo ParamInfo;
    if (!Pin)
    {
        return ParamInfo;
    }

    ParamInfo.TypeText = BuildSignatureTypeText_BP(Pin->PinType);
    ParamInfo.Name = GetMetaPinDisplayName_BP(Pin);
    ParamInfo.DefaultValue = FormatPinDefaultValueForMeta_BP(Pin);
    return ParamInfo;
}

FString NormalizeMetaCppTypeText_BP(const FString& InTypeText)
{
    FString TypeText = InTypeText;
    if (TypeText.StartsWith(TEXT("const ")))
    {
        TypeText.RightChopInline(6, EAllowShrinking::No);
    }

    return TypeText;
}

FString FormatZeroValueForMetaProperty_BP(const FProperty* Property, const void* ContainerPtr)
{
    if (!Property || !ContainerPtr)
    {
        return TEXT("None");
    }

    const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
    if (!ValuePtr)
    {
        return TEXT("None");
    }

    if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
    {
        if (StructProperty->Struct)
        {
            const FString StructName = StructProperty->Struct->GetName();
            if (StructName == TEXT("InputActionValue"))
            {
                return TEXT("()");
            }

            if (StructName == TEXT("Key"))
            {
                return TEXT("None");
            }
        }
    }

    const FString ExportedText = ExportPropertyText_BP(Property, ValuePtr);
    if (!ExportedText.IsEmpty())
    {
        return ExportedText;
    }

    return FormatMetaPropertyValue_BP(Property, ValuePtr, EBlueprintMetaValueStyle::VariableCompact, 1, 8);
}

FBlueprintMetaParamInfo BuildMetaParamInfoFromProperty_BP(const FProperty* Property, const void* ContainerPtr)
{
    FBlueprintMetaParamInfo ParamInfo;
    if (!Property || !ContainerPtr)
    {
        return ParamInfo;
    }

    FString TypeText = NormalizeMetaCppTypeText_BP(FormatPropertyCppTypeForMeta_BP(Property));
    if (Property->HasAnyPropertyFlags(CPF_ReferenceParm) && !TypeText.EndsWith(TEXT("&")))
    {
        TypeText += TEXT("&");
    }

    ParamInfo.TypeText = TypeText;
    ParamInfo.Name = Property->GetAuthoredName();
    ParamInfo.DefaultValue = FormatZeroValueForMetaProperty_BP(Property, ContainerPtr);
    return ParamInfo;
}

FBlueprintMetaParamInfo MakeGeneratedMetaParamInfo_BP(const FString& TypeText, const FString& Name, const FString& DefaultValue)
{
    FBlueprintMetaParamInfo ParamInfo;
    ParamInfo.TypeText = TypeText;
    ParamInfo.Name = Name;
    ParamInfo.DefaultValue = DefaultValue;
    return ParamInfo;
}

void AppendEnhancedInputActionEventParams_BP(FBlueprintMetaEventInfo& EventInfo)
{
    EventInfo.Inputs.Add(MakeGeneratedMetaParamInfo_BP(TEXT("FInputActionValue"), TEXT("ActionValue"), TEXT("()")));
    EventInfo.Inputs.Add(MakeGeneratedMetaParamInfo_BP(TEXT("float"), TEXT("ElapsedTime"), TEXT("0.000000")));
    EventInfo.Inputs.Add(MakeGeneratedMetaParamInfo_BP(TEXT("float"), TEXT("TriggeredTime"), TEXT("0.000000")));
    EventInfo.Inputs.Add(MakeGeneratedMetaParamInfo_BP(TEXT("UInputAction*"), TEXT("SourceAction"), TEXT("None")));
}

void AppendInputKeyEventParams_BP(FBlueprintMetaEventInfo& EventInfo)
{
    EventInfo.Inputs.Add(MakeGeneratedMetaParamInfo_BP(TEXT("FKey"), TEXT("Key"), TEXT("None")));
}

bool IsFunctionInputPinForMeta_BP(const UEdGraphPin* Pin)
{
    return Pin &&
        Pin->ParentPin == nullptr &&
        Pin->Direction == EGPD_Output &&
        Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
        Pin->PinName != TEXT("then");
}

bool IsFunctionOutputPinForMeta_BP(const UEdGraphPin* Pin)
{
    return Pin &&
        Pin->ParentPin == nullptr &&
        Pin->Direction == EGPD_Input &&
        Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
        Pin->PinName != TEXT("exec");
}

bool IsEventDataPinForMeta_BP(const UEdGraphPin* Pin)
{
    return Pin &&
        Pin->Direction == EGPD_Output &&
        Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
        Pin->PinName != UK2Node_Event::DelegateOutputName;
}

FString FormatMetaParamEntry_BP(const FBlueprintMetaParamInfo& ParamInfo)
{
    if (ParamInfo.DefaultValue.IsEmpty())
    {
        return FString::Printf(TEXT("%s %s"), *ParamInfo.TypeText, *ParamInfo.Name);
    }

    return FString::Printf(TEXT("%s %s = %s"), *ParamInfo.TypeText, *ParamInfo.Name, *ParamInfo.DefaultValue);
}

FString FormatMetaParamGroup_BP(const TArray<FBlueprintMetaParamInfo>& Params, const FString& EmptyText, const FString& Label)
{
    if (Params.Num() == 0)
    {
        return EmptyText;
    }

    TArray<FString> Entries;
    Entries.Reserve(Params.Num());
    for (const FBlueprintMetaParamInfo& ParamInfo : Params)
    {
        Entries.Add(FormatMetaParamEntry_BP(ParamInfo));
    }

    return FString::Printf(TEXT("%s(%s)"), *Label, *FString::Join(Entries, TEXT(",")));
}

FString FormatMetaFunctionSignature_BP(const FBlueprintMetaFunctionInfo& FunctionInfo)
{
    return FString::Printf(
        TEXT("- %s [%s, %s]"),
        *FunctionInfo.Name,
        *FormatMetaParamGroup_BP(FunctionInfo.Inputs, TEXT("No Inputs"), TEXT("Inputs")),
        *FormatMetaParamGroup_BP(FunctionInfo.Outputs, TEXT("No Outputs"), TEXT("Outputs")));
}

FString FormatMetaEventSignature_BP(const FBlueprintMetaEventInfo& EventInfo)
{
    FString Signature = FString::Printf(
        TEXT("- %s [%s]"),
        *EventInfo.Name,
        *FormatMetaParamGroup_BP(EventInfo.Inputs, TEXT("No Inputs"), TEXT("Inputs")));

    if (!EventInfo.ReplicationText.IsEmpty())
    {
        Signature += FString::Printf(TEXT(" (Replicates: %s)"), *EventInfo.ReplicationText);
    }

    return Signature;
}

FString BuildReplicationTextFromFlags_BP(const uint32 Flags)
{
    if ((Flags & FUNC_Net) == 0)
    {
        return FString();
    }

    FString ModeText;
    if ((Flags & FUNC_NetClient) != 0)
    {
        ModeText = TEXT("Run on owning Client");
    }
    else if ((Flags & FUNC_NetServer) != 0)
    {
        ModeText = TEXT("Run on Server");
    }
    else if ((Flags & FUNC_NetMulticast) != 0)
    {
        ModeText = TEXT("Multicast");
    }
    else
    {
        ModeText = TEXT("Replicated");
    }

    const FString ReliabilityText = (Flags & FUNC_NetReliable) != 0 ? TEXT("Reliable") : TEXT("Unreliable");
    return FString::Printf(TEXT("%s | %s"), *ModeText, *ReliabilityText);
}

bool ShouldWrapGeneratedEventName_BP(const FString& EventName)
{
    return EventName.StartsWith(TEXT("InpActEvt_")) ||
        EventName.StartsWith(TEXT("BndEvt__")) ||
        EventName.StartsWith(TEXT("OnCompleted_")) ||
        EventName.StartsWith(TEXT("OnBlendOut_")) ||
        EventName.StartsWith(TEXT("OnInterrupted_")) ||
        EventName.StartsWith(TEXT("OnNotifyBegin_")) ||
        EventName.StartsWith(TEXT("OnNotifyEnd_"));
}

FString NormalizeNativeEventNameForMeta_BP(const FString& EventName)
{
    if (EventName.StartsWith(TEXT("Receive")) && EventName.Len() > UE_ARRAY_COUNT(TEXT("Receive")) - 1)
    {
        return EventName.RightChop(UE_ARRAY_COUNT(TEXT("Receive")) - 1);
    }

    return EventName;
}

void AppendGeneratedBoundEvent_BP(
    const FName FunctionName,
    UClass* FunctionOwnerClass,
    const UDynamicBlueprintBinding* BindingObject,
    TSet<FString>& SeenEventNames,
    TArray<FBlueprintMetaEventInfo>& OutEvents)
{
    if (FunctionName.IsNone())
    {
        return;
    }

    FString EventName = FunctionName.ToString();
    if (ShouldWrapGeneratedEventName_BP(EventName))
    {
        EventName = FString::Printf(TEXT("$$ %s $$"), *EventName);
    }
    else
    {
        EventName = NormalizeNativeEventNameForMeta_BP(EventName);
    }

    const FString LookupKey = NormalizeLookupKey(EventName);
    if (SeenEventNames.Contains(LookupKey))
    {
        return;
    }

    SeenEventNames.Add(LookupKey);

    FBlueprintMetaEventInfo EventInfo;
    EventInfo.Name = EventName;

    if (BindingObject && BindingObject->IsA<UEnhancedInputActionDelegateBinding>())
    {
        AppendEnhancedInputActionEventParams_BP(EventInfo);
    }
    else if (BindingObject && BindingObject->IsA<UInputKeyDelegateBinding>())
    {
        AppendInputKeyEventParams_BP(EventInfo);
    }
    else if (UFunction* Function = FunctionOwnerClass ? FunctionOwnerClass->FindFunctionByName(FunctionName) : nullptr)
    {
        TArray<uint8> ParamStorage;
        ParamStorage.AddZeroed(Function->ParmsSize);
        void* ParamContainer = ParamStorage.Num() > 0 ? ParamStorage.GetData() : nullptr;

        for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & CPF_Parm); ++It)
        {
            const FProperty* ParamProperty = *It;
            if (!ParamProperty || ParamProperty->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                continue;
            }

            EventInfo.Inputs.Add(BuildMetaParamInfoFromProperty_BP(ParamProperty, ParamContainer));
        }
    }

    OutEvents.Add(MoveTemp(EventInfo));
}

void AppendGeneratedBindingEvents_BP(UBlueprint* Blueprint, TSet<FString>& SeenEventNames, TArray<FBlueprintMetaEventInfo>& OutEvents)
{
    if (!Blueprint)
    {
        return;
    }

    UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
    if (!GeneratedClass)
    {
        GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->SkeletonGeneratedClass);
    }
    if (!GeneratedClass)
    {
        return;
    }

    UClass* FunctionOwnerClass = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass : GeneratedClass;
    if (!FunctionOwnerClass)
    {
        FunctionOwnerClass = GeneratedClass;
    }

    for (UDynamicBlueprintBinding* BindingObject : GeneratedClass->DynamicBindingObjects)
    {
        if (!BindingObject)
        {
            continue;
        }

        if (const UEnhancedInputActionDelegateBinding* EnhancedBinding = Cast<UEnhancedInputActionDelegateBinding>(BindingObject))
        {
            for (const FBlueprintEnhancedInputActionBinding& Binding : EnhancedBinding->InputActionDelegateBindings)
            {
                AppendGeneratedBoundEvent_BP(Binding.FunctionNameToBind, FunctionOwnerClass, BindingObject, SeenEventNames, OutEvents);
            }
            continue;
        }

        if (const UInputKeyDelegateBinding* InputKeyBinding = Cast<UInputKeyDelegateBinding>(BindingObject))
        {
            for (const FBlueprintInputKeyDelegateBinding& Binding : InputKeyBinding->InputKeyDelegateBindings)
            {
                AppendGeneratedBoundEvent_BP(Binding.FunctionNameToBind, FunctionOwnerClass, BindingObject, SeenEventNames, OutEvents);
            }
        }
    }
}

void AddValueTextWithPrefix_BP(TArray<FString>& OutLines, const FString& Prefix, const FString& ValueText)
{
    TArray<FString> ValueLines;
    ValueText.ParseIntoArrayLines(ValueLines, false);
    if (ValueLines.Num() == 0)
    {
        OutLines.Add(Prefix);
        return;
    }

    OutLines.Add(Prefix + ValueLines[0]);
    // Indent continuation lines to align under the value (not the key).
    const FString ContinuationIndent = FString::ChrN(Prefix.Len(), ' ');
    for (int32 Index = 1; Index < ValueLines.Num(); ++Index)
    {
        OutLines.Add(ContinuationIndent + ValueLines[Index]);
    }
}

void AddMetaSection_BP(TArray<FString>& OutLines, const FString& Header, const TArray<FString>& Entries)
{
    if (Entries.Num() == 0)
    {
        return;
    }

    OutLines.Add(Header);
    for (const FString& Entry : Entries)
    {
        OutLines.Add(Entry);
    }
    OutLines.Add(FString());
}

FString BuildRawGraphTextForPython_BP(UEdGraph* Graph);

void AddIndentedMetaLine_BP(TArray<FString>& OutLines, const int32 IndentLevel, const FString& Line)
{
    OutLines.Add(FString::ChrN(FMath::Max(0, IndentLevel) * 2, ' ') + Line);
}

void AddIndentedMetaTextBlock_BP(TArray<FString>& OutLines, const int32 IndentLevel, const FString& Text)
{
    TArray<FString> BlockLines;
    Text.ParseIntoArrayLines(BlockLines, false);
    if (BlockLines.Num() == 0)
    {
        AddIndentedMetaLine_BP(OutLines, IndentLevel, TEXT("None"));
        return;
    }

    for (const FString& BlockLine : BlockLines)
    {
        AddIndentedMetaLine_BP(OutLines, IndentLevel, BlockLine);
    }
}

FString FormatMetaInlineText_BP(const FString& Value)
{
    return Value.IsEmpty() ? TEXT("None") : QuoteMetaString_BP(Value);
}

FString SanitizeMetaGraphInlineText_BP(const FString& Value)
{
    FString Result = Value;
    Result.ReplaceInline(TEXT("\r"), TEXT(" "));
    Result.ReplaceInline(TEXT("\n"), TEXT(" "));
    Result.TrimStartAndEndInline();
    while (Result.ReplaceInline(TEXT("  "), TEXT(" "), ESearchCase::CaseSensitive) > 0)
    {
    }

    return Result.IsEmpty() ? TEXT("None") : Result;
}

FString GetMetaGraphKind_BP(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    auto ContainsGraph = [Graph](const TArray<TObjectPtr<UEdGraph>>& Graphs) -> bool
    {
        for (const UEdGraph* Candidate : Graphs)
        {
            if (Candidate == Graph)
            {
                return true;
            }
        }
        return false;
    };

    if (!Blueprint || !Graph)
    {
        return TEXT("Unknown");
    }

    if (ContainsGraph(Blueprint->UbergraphPages))
    {
        return TEXT("Ubergraph");
    }
    if (ContainsGraph(Blueprint->FunctionGraphs))
    {
        return TEXT("Function");
    }
    if (ContainsGraph(Blueprint->DelegateSignatureGraphs))
    {
        return TEXT("DelegateSignature");
    }
    if (ContainsGraph(Blueprint->MacroGraphs))
    {
        return TEXT("Macro");
    }

    const FString GraphClassName = Graph->GetClass()->GetName();
    if (GraphClassName == TEXT("AnimationStateMachineGraph"))
    {
        return TEXT("AnimStateMachine");
    }
    if (GraphClassName == TEXT("AnimationStateGraph"))
    {
        return TEXT("AnimState");
    }
    if (GraphClassName == TEXT("AnimationTransitionGraph"))
    {
        return TEXT("AnimTransition");
    }
    if (GraphClassName == TEXT("AnimationConduitGraph"))
    {
        return TEXT("AnimConduit");
    }
    if (GraphClassName.Contains(TEXT("AnimationGraph")))
    {
        return TEXT("AnimGraph");
    }

    return TEXT("Other");
}

FString GetMetaGraphNodeId_BP(const UEdGraphNode* Node)
{
    if (!Node)
    {
        return TEXT("None");
    }

    if (!Node->GetName().IsEmpty())
    {
        return Node->GetName();
    }

    return Node->NodeGuid.IsValid() ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower) : TEXT("None");
}

FString GetMetaGraphPinType_BP(const UEdGraphPin* Pin)
{
    return Pin ? BuildSignatureTypeText_BP(Pin->PinType) : TEXT("None");
}

FString BuildCompactMetaGraphHeader_BP(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    const FString GraphKind = GetMetaGraphKind_BP(Blueprint, Graph);
    const FString GraphName = Graph ? Graph->GetName() : TEXT("None");

    if (GraphKind == TEXT("Ubergraph"))
    {
        return FString::Printf(TEXT("- Ubergraph: %s"), *GraphName);
    }
    if (GraphKind == TEXT("Function"))
    {
        return FString::Printf(TEXT("- Function Graph: %s"), *GraphName);
    }
    if (GraphKind == TEXT("Macro"))
    {
        return FString::Printf(TEXT("- Macro Graph: %s"), *GraphName);
    }
    if (GraphKind == TEXT("DelegateSignature"))
    {
        return FString::Printf(TEXT("- Delegate Signature Graph: %s"), *GraphName);
    }

    return FString::Printf(TEXT("- %s Graph: %s"), *GraphKind, *GraphName);
}

FString FormatCompactPinDefaultValueForMeta_BP(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return TEXT("None");
    }

    if (Pin->DefaultObject)
    {
        return Pin->DefaultObject->GetName();
    }

    return SanitizeMetaGraphInlineText_BP(FormatPinDefaultValueForMeta_BP(Pin));
}

FString BuildCompactPinDefaultEntry_BP(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return FString();
    }

    return FString::Printf(
        TEXT("`%s`=%s"),
        *SanitizeMetaGraphInlineText_BP(GetMetaPinDisplayName_BP(Pin)),
        *FormatCompactPinDefaultValueForMeta_BP(Pin));
}

FString GetCompactMetaConnectionLabel_BP(const UEdGraphPin* Pin, const bool bIsSourcePin)
{
    if (!Pin)
    {
        return TEXT("None");
    }

    FString Label = SanitizeMetaGraphInlineText_BP(GetMetaPinDisplayName_BP(Pin));
    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
    {
        if (bIsSourcePin)
        {
            if (Label.Equals(TEXT("then"), ESearchCase::IgnoreCase) ||
                Label.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
                Label.Equals(TEXT("exec"), ESearchCase::IgnoreCase))
            {
                return TEXT("(ThisNode)");
            }
            return Label;
        }

        if (Label.Equals(TEXT("then"), ESearchCase::IgnoreCase) ||
            Label.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
            Label.Equals(TEXT("exec"), ESearchCase::IgnoreCase))
        {
            return TEXT("Exec");
        }
        return Label;
    }

    return FString::Printf(TEXT("`%s`"), *Label);
}

FString BuildCompactMetaConnectionEntry_BP(const UEdGraphPin* SourcePin, const UEdGraphPin* TargetPin)
{
    if (!SourcePin || !TargetPin || !TargetPin->GetOwningNode())
    {
        return FString();
    }

    return FString::Printf(
        TEXT("%s->%s(%s)"),
        *GetCompactMetaConnectionLabel_BP(SourcePin, true),
        *GetCompactMetaConnectionLabel_BP(TargetPin, false),
        *GetMetaGraphNodeId_BP(TargetPin->GetOwningNode()));
}

FString BuildCompactMetaNodeLine_BP(const UEdGraphNode* Node)
{
    if (!Node)
    {
        return TEXT("Node: None");
    }

    return FString::Printf(
        TEXT("Node: %s (%s) [%s]"),
        *SanitizeMetaGraphInlineText_BP(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()),
        *GetMetaGraphNodeId_BP(Node),
        *Node->GetClass()->GetName());
}

bool ShouldEmitMetaPinDefaults_BP(const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return false;
    }

    if (Pin->Direction != EGPD_Input)
    {
        return false;
    }

    const FString PinName = Pin->PinName.ToString();
    if (PinName.Equals(TEXT("self"), ESearchCase::IgnoreCase) ||
        Pin->PinName == UEdGraphSchema_K2::PN_Self)
    {
        return false;
    }

    if (Pin->DefaultObject || !Pin->DefaultTextValue.IsEmpty() || !Pin->DefaultValue.IsEmpty() || !Pin->AutogeneratedDefaultValue.IsEmpty())
    {
        return true;
    }

    const FString MetaDefault = FormatPinDefaultValueForMeta_BP(Pin);
    return !MetaDefault.IsEmpty() && !MetaDefault.Equals(TEXT("None"), ESearchCase::IgnoreCase);
}

void AppendMetaPinDefaults_BP(TArray<FString>& OutLines, const int32 IndentLevel, const UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return;
    }

    AddIndentedMetaLine_BP(
        OutLines,
        IndentLevel,
        FString::Printf(
            TEXT("- %s | %s | %s"),
            *GetMetaPinDisplayName_BP(Pin),
            Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"),
            *GetMetaGraphPinType_BP(Pin)));

    const FString DefaultValue = Pin->DefaultValue;
    const FString AutogeneratedDefaultValue = Pin->AutogeneratedDefaultValue;
    const FString DefaultObjectPath = Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : FString();
    const FString DefaultTextValue = Pin->DefaultTextValue.IsEmpty() ? FString() : Pin->DefaultTextValue.ToString();
    const FString ResolvedDefaultValue = FormatPinDefaultValueForMeta_BP(Pin);

    if (!DefaultValue.IsEmpty())
    {
        AddIndentedMetaLine_BP(OutLines, IndentLevel + 1, FString::Printf(TEXT("default_value: %s"), *FormatMetaInlineText_BP(DefaultValue)));
    }

    if (!AutogeneratedDefaultValue.IsEmpty() && AutogeneratedDefaultValue != DefaultValue)
    {
        AddIndentedMetaLine_BP(
            OutLines,
            IndentLevel + 1,
            FString::Printf(TEXT("autogenerated_default_value: %s"), *FormatMetaInlineText_BP(AutogeneratedDefaultValue)));
    }

    if (!DefaultObjectPath.IsEmpty())
    {
        AddIndentedMetaLine_BP(
            OutLines,
            IndentLevel + 1,
            FString::Printf(TEXT("default_object: %s"), *FormatMetaInlineText_BP(DefaultObjectPath)));
    }

    if (!DefaultTextValue.IsEmpty() && DefaultTextValue != DefaultValue)
    {
        AddIndentedMetaLine_BP(
            OutLines,
            IndentLevel + 1,
            FString::Printf(TEXT("default_text_value: %s"), *FormatMetaInlineText_BP(DefaultTextValue)));
    }

    if (!ResolvedDefaultValue.IsEmpty() &&
        !ResolvedDefaultValue.Equals(TEXT("None"), ESearchCase::IgnoreCase) &&
        ResolvedDefaultValue != DefaultValue &&
        ResolvedDefaultValue != AutogeneratedDefaultValue &&
        ResolvedDefaultValue != DefaultTextValue &&
        ResolvedDefaultValue != DefaultObjectPath)
    {
        AddIndentedMetaLine_BP(
            OutLines,
            IndentLevel + 1,
            FString::Printf(TEXT("resolved_default: %s"), *FormatMetaInlineText_BP(ResolvedDefaultValue)));
    }
}

void AppendMetaGraphDetails_BP(TArray<FString>& OutLines, UBlueprint* Blueprint, UEdGraph* Graph, const FBlueprintMetaExportOptions& Options)
{
    if (!Blueprint || !Graph)
    {
        return;
    }

    AddIndentedMetaLine_BP(OutLines, 0, BuildCompactMetaGraphHeader_BP(Blueprint, Graph));

    TMap<FString, TArray<FString>> ConnectionsBySourceNode;
    TSet<FString> SeenConnections;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node || Node->GetClass()->GetName().Equals(TEXT("EdGraphNode_Comment"), ESearchCase::IgnoreCase))
        {
            continue;
        }

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin)
                {
                    continue;
                }

                const UEdGraphNode* PinNode = Pin->GetOwningNode();
                const UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                if (!PinNode || !LinkedNode)
                {
                    continue;
                }

                const UEdGraphPin* SourcePin = Pin;
                const UEdGraphPin* TargetPin = LinkedPin;

                if (Pin->Direction == EGPD_Input && LinkedPin->Direction == EGPD_Output)
                {
                    SourcePin = LinkedPin;
                    TargetPin = Pin;
                }
                else if (Pin->Direction == LinkedPin->Direction)
                {
                    const FString PinRef = FString::Printf(TEXT("%s.%s"), *PinNode->GetName(), *Pin->PinName.ToString());
                    const FString LinkedRef = FString::Printf(TEXT("%s.%s"), *LinkedNode->GetName(), *LinkedPin->PinName.ToString());
                    if (PinRef > LinkedRef)
                    {
                        SourcePin = LinkedPin;
                        TargetPin = Pin;
                    }
                }

                const UEdGraphNode* SourceNode = SourcePin->GetOwningNode();
                const UEdGraphNode* TargetNode = TargetPin->GetOwningNode();
                if (!SourceNode || !TargetNode)
                {
                    continue;
                }

                const FString ConnectionKey = FString::Printf(
                    TEXT("%s.%s->%s.%s"),
                    *SourceNode->GetName(),
                    *SourcePin->PinName.ToString(),
                    *TargetNode->GetName(),
                    *TargetPin->PinName.ToString());

                if (SeenConnections.Contains(ConnectionKey))
                {
                    continue;
                }

                SeenConnections.Add(ConnectionKey);
                ConnectionsBySourceNode.FindOrAdd(SourceNode->GetName()).Add(
                    BuildCompactMetaConnectionEntry_BP(SourcePin, TargetPin));
            }
        }
    }

    int32 EmittedNodeCount = 0;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node || Node->GetClass()->GetName().Equals(TEXT("EdGraphNode_Comment"), ESearchCase::IgnoreCase))
        {
            continue;
        }

        ++EmittedNodeCount;
        AddIndentedMetaLine_BP(OutLines, 1, BuildCompactMetaNodeLine_BP(Node));

        TArray<FString> DefaultEntries;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (ShouldEmitMetaPinDefaults_BP(Pin))
            {
                DefaultEntries.Add(BuildCompactPinDefaultEntry_BP(Pin));
            }
        }

        if (DefaultEntries.Num() > 0)
        {
            AddIndentedMetaLine_BP(
                OutLines,
                2,
                FString::Printf(TEXT("defaults: %s"), *FString::Join(DefaultEntries, TEXT("; "))));
        }

        if (const TArray<FString>* ConnectionEntries = ConnectionsBySourceNode.Find(Node->GetName()))
        {
            AddIndentedMetaLine_BP(
                OutLines,
                2,
                FString::Printf(TEXT("links: %s"), *FString::Join(*ConnectionEntries, TEXT("; "))));
        }
    }

    if (EmittedNodeCount == 0)
    {
        AddIndentedMetaLine_BP(OutLines, 1, TEXT("Node: None"));
    }

    if (Options.bIncludeRawGraphPayload)
    {
        AddIndentedMetaLine_BP(OutLines, 1, TEXT("raw_graph_payload:"));
        const FString RawGraphText = BuildRawGraphTextForPython_BP(Graph);
        if (RawGraphText.IsEmpty())
        {
            AddIndentedMetaLine_BP(OutLines, 2, TEXT("# No nodes exported"));
        }
        else
        {
            AddIndentedMetaTextBlock_BP(OutLines, 2, RawGraphText);
        }
    }
}

void CollectMetaGraphLines_BP(UBlueprint* Blueprint, const FBlueprintMetaExportOptions& Options, TArray<FString>& OutLines)
{
    OutLines.Reset();
    if (!Blueprint)
    {
        return;
    }

    auto AppendGraphs = [&OutLines, Blueprint, &Options](const TArray<TObjectPtr<UEdGraph>>& Graphs)
    {
        for (UEdGraph* Graph : Graphs)
        {
            AppendMetaGraphDetails_BP(OutLines, Blueprint, Graph, Options);
        }
    };

    AppendGraphs(Blueprint->UbergraphPages);
    AppendGraphs(Blueprint->FunctionGraphs);
    AppendGraphs(Blueprint->DelegateSignatureGraphs);
    AppendGraphs(Blueprint->MacroGraphs);
}

void CollectMetaFunctions_BP(UBlueprint* Blueprint, TArray<FBlueprintMetaFunctionInfo>& OutFunctions)
{
    OutFunctions.Reset();
    if (!Blueprint)
    {
        return;
    }

    TSet<FString> SeenNames;

    if (UEdGraph* UserConstructionScript = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
    {
        FBlueprintMetaFunctionInfo ConstructionInfo;
        ConstructionInfo.Name = TEXT("Construction Script");
        OutFunctions.Add(ConstructionInfo);
        SeenNames.Add(NormalizeLookupKey(ConstructionInfo.Name));
    }

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph)
        {
            continue;
        }

        if (Graph == FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
        {
            continue;
        }

        const FString FunctionName = Graph->GetName();
        const FString LookupKey = NormalizeLookupKey(FunctionName);
        if (SeenNames.Contains(LookupKey))
        {
            continue;
        }

        FBlueprintMetaFunctionInfo FunctionInfo;
        FunctionInfo.Name = FunctionName;

        TSet<FString> SeenOutputs;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
            {
                for (UEdGraphPin* Pin : EntryNode->Pins)
                {
                    if (IsFunctionInputPinForMeta_BP(Pin))
                    {
                        FunctionInfo.Inputs.Add(BuildMetaParamInfoFromPin_BP(Pin));
                    }
                }
            }
            else if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
            {
                for (UEdGraphPin* Pin : ResultNode->Pins)
                {
                    if (!IsFunctionOutputPinForMeta_BP(Pin))
                    {
                        continue;
                    }

                    const FString Key = NormalizeLookupKey(Pin->PinName.ToString());
                    if (SeenOutputs.Contains(Key))
                    {
                        continue;
                    }

                    SeenOutputs.Add(Key);
                    FunctionInfo.Outputs.Add(BuildMetaParamInfoFromPin_BP(Pin));
                }
            }
        }

        SeenNames.Add(LookupKey);
        OutFunctions.Add(FunctionInfo);
    }
}

void CollectMetaEvents_BP(UBlueprint* Blueprint, TArray<FBlueprintMetaEventInfo>& OutEvents)
{
    OutEvents.Reset();
    if (!Blueprint)
    {
        return;
    }

    UClass* BlueprintFunctionOwner = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass : Blueprint->GeneratedClass;

    TSet<FString> SeenEventNames;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (!Graph)
        {
            continue;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
            if (!EventNode)
            {
                continue;
            }

            FString EventName;
            FString ReplicationText;

            if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(EventNode))
            {
                EventName = CustomEventNode->CustomFunctionName.IsNone()
                    ? CustomEventNode->GetFunctionName().ToString()
                    : CustomEventNode->CustomFunctionName.ToString();

                if (ShouldWrapGeneratedEventName_BP(EventName))
                {
                    EventName = FString::Printf(TEXT("$$ %s $$"), *EventName);
                }
                else if (!EventName.StartsWith(TEXT("$$")) && !EventName.StartsWith(TEXT("Event ")))
                {
                    EventName = FString::Printf(TEXT("Event %s"), *EventName);
                }

                ReplicationText = BuildReplicationTextFromFlags_BP(CustomEventNode->GetNetFlags());
            }
            else if (UK2Node_ComponentBoundEvent* ComponentBoundEvent = Cast<UK2Node_ComponentBoundEvent>(EventNode))
            {
                EventName = ComponentBoundEvent->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
                ReplicationText = BuildReplicationTextFromFlags_BP(ComponentBoundEvent->FunctionFlags);
            }
            else
            {
                EventName = EventNode->GetFunctionName().ToString();
                EventName = NormalizeNativeEventNameForMeta_BP(EventName);
                if (EventName.IsEmpty())
                {
                    EventName = EventNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
                    if (EventName.StartsWith(TEXT("Event ")))
                    {
                        EventName.RightChopInline(6, EAllowShrinking::No);
                    }
                }
                ReplicationText = BuildReplicationTextFromFlags_BP(EventNode->FunctionFlags);
            }

            if (EventName.IsEmpty())
            {
                continue;
            }

            const FString LookupKey = NormalizeLookupKey(EventName);
            if (SeenEventNames.Contains(LookupKey))
            {
                continue;
            }

            SeenEventNames.Add(LookupKey);

            FBlueprintMetaEventInfo EventInfo;
            EventInfo.Name = EventName;
            EventInfo.ReplicationText = ReplicationText;

            UFunction* SignatureFunction = nullptr;
            if (!EventNode->EventReference.GetMemberName().IsNone())
            {
                SignatureFunction = EventNode->EventReference.ResolveMember<UFunction>(EventNode->GetBlueprintClassFromNode());
            }

            if (!SignatureFunction && BlueprintFunctionOwner && !EventNode->GetFunctionName().IsNone())
            {
                SignatureFunction = BlueprintFunctionOwner->FindFunctionByName(EventNode->GetFunctionName());
            }

            if (SignatureFunction)
            {
                TArray<uint8> ParamStorage;
                ParamStorage.AddZeroed(SignatureFunction->ParmsSize);
                void* ParamContainer = ParamStorage.Num() > 0 ? ParamStorage.GetData() : nullptr;

                for (TFieldIterator<FProperty> It(SignatureFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
                {
                    const FProperty* ParamProperty = *It;
                    if (!ParamProperty || ParamProperty->HasAnyPropertyFlags(CPF_ReturnParm))
                    {
                        continue;
                    }

                    EventInfo.Inputs.Add(BuildMetaParamInfoFromProperty_BP(ParamProperty, ParamContainer));
                }
            }
            else for (UEdGraphPin* Pin : EventNode->Pins)
            {
                if (IsEventDataPinForMeta_BP(Pin))
                {
                    EventInfo.Inputs.Add(BuildMetaParamInfoFromPin_BP(Pin));
                }
            }

            OutEvents.Add(EventInfo);
        }
    }

    AppendGeneratedBindingEvents_BP(Blueprint, SeenEventNames, OutEvents);
}

void AppendMetaComponentTree_BP(USCS_Node* Node, int32& InOutOrderIndex, TArray<FBlueprintMetaComponentInfo>& OutComponents, TSet<FString>& SeenNames, const FString& ParentName = FString())
{
    if (!Node || !Node->ComponentTemplate)
    {
        return;
    }

    const FString ComponentName = Node->GetVariableName().ToString();
    const FString LookupKey = NormalizeLookupKey(ComponentName);
    if (!SeenNames.Contains(LookupKey))
    {
        SeenNames.Add(LookupKey);

        FBlueprintMetaComponentInfo ComponentInfo;
        ComponentInfo.Name = ComponentName;
        ComponentInfo.ClassName = Node->ComponentTemplate->GetClass()->GetName();
        ComponentInfo.ParentName = ParentName;
        ComponentInfo.OrderIndex = InOutOrderIndex++;
        OutComponents.Add(ComponentInfo);
    }

    for (USCS_Node* ChildNode : Node->GetChildNodes())
    {
        AppendMetaComponentTree_BP(ChildNode, InOutOrderIndex, OutComponents, SeenNames, ComponentName);
    }
}

void CollectMetaComponents_BP(UBlueprint* Blueprint, TArray<FBlueprintMetaComponentInfo>& OutComponents)
{
    OutComponents.Reset();
    if (!Blueprint)
    {
        return;
    }

    TSet<FString> SeenNames;
    int32 OrderIndex = 0;

    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* RootNode : Blueprint->SimpleConstructionScript->GetRootNodes())
        {
            AppendMetaComponentTree_BP(RootNode, OrderIndex, OutComponents, SeenNames);
        }
    }

    TArray<FBlueprintComponentHandle> Components;
    CollectBlueprintComponents_BP(Blueprint, Components);
    for (const FBlueprintComponentHandle& Component : Components)
    {
        const FString LookupKey = NormalizeLookupKey(Component.Name);
        if (SeenNames.Contains(LookupKey) || !Component.Object)
        {
            continue;
        }

        SeenNames.Add(LookupKey);

        FBlueprintMetaComponentInfo ComponentInfo;
        ComponentInfo.Name = Component.Name;
        ComponentInfo.ClassName = Component.Object->GetClass()->GetName();
        ComponentInfo.OrderIndex = OrderIndex++;
        OutComponents.Add(ComponentInfo);
    }
}

FString BuildBlueprintMetaText_BP(UBlueprint* Blueprint, const FBlueprintMetaExportOptions& Options)
{
    TArray<FString> Lines;
    if (!Blueprint)
    {
        return FString();
    }

    Lines.Add(FString::Printf(TEXT("Blueprint Name: %s"), *Blueprint->GetName()));
    Lines.Add(FString::Printf(TEXT("Path: %s"), *Blueprint->GetPathName()));
    Lines.Add(FString());
    Lines.Add(FString::Printf(TEXT("Parent Class: %s"), Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("None")));
    Lines.Add(FString());

    if (ShouldIncludeMetaPart_BP(Options, TEXT("interfaces")))
    {
        TArray<FString> InterfaceLines;
        for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
        {
            InterfaceLines.Add(FString::Printf(TEXT("- %s"), InterfaceDescription.Interface ? *InterfaceDescription.Interface->GetName() : TEXT("Unknown")));
        }
        AddMetaSection_BP(Lines, TEXT("Implemented Interfaces:"), InterfaceLines);
    }

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(Blueprint);
    UObject* BlueprintCDO = BlueprintClass ? BlueprintClass->GetDefaultObject() : nullptr;

    if (ShouldIncludeMetaPart_BP(Options, TEXT("propertydeclarations")))
    {
        TArray<FString> VariableLines;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            FString ValueText = Variable.DefaultValue;
            if (BlueprintCDO)
            {
                if (FProperty* Property = FindPropertyByNameLoose(BlueprintCDO->GetClass(), Variable.VarName.ToString()))
                {
                    if (const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(BlueprintCDO))
                    {
                        ValueText = FormatMetaPropertyValue_BP(
                            Property,
                            ValuePtr,
                            EBlueprintMetaValueStyle::VariableCompact,
                            Options.PropertyValueMaxDepth,
                            Options.PropertyValueMaxItems);
                    }
                }
            }

            if (ValueText.IsEmpty())
            {
                ValueText = TEXT("None");
            }

            AddValueTextWithPrefix_BP(
                VariableLines,
                FString::Printf(TEXT("- %s (%s) = "), *Variable.VarName.ToString(), *BuildVariableTypeText_BP(Variable.VarType)),
                ValueText);
        }
        AddMetaSection_BP(Lines, TEXT("Variables:"), VariableLines);
    }

    if (ShouldIncludeMetaPart_BP(Options, TEXT("components")))
    {
        TArray<FBlueprintMetaComponentInfo> Components;
        CollectMetaComponents_BP(Blueprint, Components);

        TMap<FString, TArray<FBlueprintMetaComponentInfo>> ChildrenByParent;
        for (const FBlueprintMetaComponentInfo& ComponentInfo : Components)
        {
            ChildrenByParent.FindOrAdd(ComponentInfo.ParentName).Add(ComponentInfo);
        }

        for (TPair<FString, TArray<FBlueprintMetaComponentInfo>>& Pair : ChildrenByParent)
        {
            Pair.Value.Sort([](const FBlueprintMetaComponentInfo& A, const FBlueprintMetaComponentInfo& B)
            {
                return A.OrderIndex < B.OrderIndex;
            });
        }

        TArray<FString> ComponentLines;
        TFunction<void(const FString&, int32)> AppendChildren = [&](const FString& ParentName, const int32 Depth)
        {
            const TArray<FBlueprintMetaComponentInfo>* Children = ChildrenByParent.Find(ParentName);
            if (!Children)
            {
                return;
            }

            for (const FBlueprintMetaComponentInfo& ComponentInfo : *Children)
            {
                ComponentLines.Add(FString::Printf(TEXT("%s- %s (%s)"), *FString::ChrN(Depth * 2, ' '), *ComponentInfo.Name, *ComponentInfo.ClassName));
                AppendChildren(ComponentInfo.Name, Depth + 1);
            }
        };

        AppendChildren(FString(), 0);
        AddMetaSection_BP(Lines, TEXT("Components:"), ComponentLines);
    }

    if (ShouldIncludeMetaPart_BP(Options, TEXT("functions")))
    {
        TArray<FBlueprintMetaFunctionInfo> Functions;
        CollectMetaFunctions_BP(Blueprint, Functions);

        TArray<FString> FunctionLines;
        for (const FBlueprintMetaFunctionInfo& FunctionInfo : Functions)
        {
            FunctionLines.Add(FormatMetaFunctionSignature_BP(FunctionInfo));
        }
        AddMetaSection_BP(Lines, TEXT("Functions:"), FunctionLines);
    }

    if (ShouldIncludeMetaPart_BP(Options, TEXT("events")))
    {
        TArray<FBlueprintMetaEventInfo> Events;
        CollectMetaEvents_BP(Blueprint, Events);

        TArray<FString> EventLines;
        for (const FBlueprintMetaEventInfo& EventInfo : Events)
        {
            EventLines.Add(FormatMetaEventSignature_BP(EventInfo));
        }
        AddMetaSection_BP(Lines, TEXT("Events:"), EventLines);
    }

    if (ShouldIncludeMetaPart_BP(Options, TEXT("macros")) && Blueprint->MacroGraphs.Num() > 0)
    {
        TArray<FString> MacroLines;
        for (UEdGraph* MacroGraph : Blueprint->MacroGraphs)
        {
            if (MacroGraph)
            {
                MacroLines.Add(FString::Printf(TEXT("- %s"), *MacroGraph->GetName()));
            }
        }
        AddMetaSection_BP(Lines, TEXT("Macros:"), MacroLines);
    }

    if (ShouldIncludeMetaPart_BP(Options, TEXT("graphs")))
    {
        TArray<FString> GraphLines;
        CollectMetaGraphLines_BP(Blueprint, Options, GraphLines);
        AddMetaSection_BP(Lines, TEXT("Graph Logic:"), GraphLines);
    }

    if (ShouldIncludeMetaPart_BP(Options, TEXT("propertyvalues")))
    {
        Lines.Add(FString::Printf(TEXT("Default Property Values for %s:"), *Blueprint->GetPathName()));
        if (BlueprintCDO)
        {
            FBlueprintPropertyQueryOptions QueryOptions;
            QueryOptions.bIncludeValues = true;
            QueryOptions.bIncludeMetadata = false;
            QueryOptions.bIncludeInherited = true;
            QueryOptions.bEditableOnly = true;

            for (TFieldIterator<FProperty> It(BlueprintCDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
            {
                FProperty* Property = *It;
                if (!ShouldIncludeProperty_BP(Property, BlueprintCDO, QueryOptions))
                {
                    continue;
                }

                const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(BlueprintCDO);
                FString ValueText = ExportPropertyText_BP(Property, ValuePtr);
                if (ValueText.IsEmpty())
                {
                    ValueText = FormatMetaPropertyValue_BP(
                        Property,
                        ValuePtr,
                        EBlueprintMetaValueStyle::VariableCompact,
                        Options.PropertyValueMaxDepth,
                        Options.PropertyValueMaxItems);
                }
                AddValueTextWithPrefix_BP(
                    Lines,
                    FString::Printf(TEXT("  %s %s = "), *FormatPropertyCppTypeForMeta_BP(Property), *Property->GetName()),
                    ValueText);
            }
        }
        Lines.Add(FString());
    }

    Lines.Add(TEXT("To get referenced struct types/layouts call get_asset_structs"));
    Lines.Add(TEXT("To get properties on components use get_blueprint_component_properties"));

    return FString::Join(Lines, TEXT("\n"));
}

FString BuildDefaultMetaExportPath_BP(UBlueprint* Blueprint)
{
    const FString BlueprintName = FPaths::MakeValidFileName(Blueprint ? Blueprint->GetName() : TEXT("Unknown"));
    return FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedBlueprints"), TEXT("meta"), BlueprintName, BlueprintName + TEXT(".meta"));
}

TArray<TSharedPtr<FJsonValue>> BuildMetaContentArray_BP(const FString& MetaText)
{
    TArray<TSharedPtr<FJsonValue>> ContentArray;
    TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
    ContentObject->SetStringField(TEXT("type"), TEXT("text"));
    ContentObject->SetStringField(TEXT("text"), MetaText);
    ContentArray.Add(MakeShared<FJsonValueObject>(ContentObject));
    return ContentArray;
}

FString BuildDefaultPythonExportPath_BP(UBlueprint* Blueprint)
{
    const FString BlueprintName = FPaths::MakeValidFileName(Blueprint ? Blueprint->GetName() : TEXT("Unknown"));
    return FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedBlueprints"), TEXT("python"), BlueprintName, BlueprintName + TEXT(".py"));
}

FString BuildDefaultBpyExportPath_BP(UBlueprint* Blueprint)
{
    const FString BlueprintName = FPaths::MakeValidFileName(Blueprint ? Blueprint->GetName() : TEXT("Unknown"));
    return FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedBlueprints"), TEXT("bpy"), BlueprintName, BlueprintName + TEXT(".bp.py"));
}

FString NormalizePythonPathForScriptLiteral_BP(const FString& InPath)
{
    return InPath.Replace(TEXT("\\"), TEXT("/"));
}

FString ResolveExistingPythonScriptPath_BP(const FString& Candidate)
{
    FString ScriptPath = Candidate;
    ScriptPath.TrimStartAndEndInline();
    if (ScriptPath.IsEmpty())
    {
        return FString();
    }

    if (FPaths::IsRelative(ScriptPath))
    {
        const FString ProjectRelativePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ScriptPath);
        if (FPaths::FileExists(ProjectRelativePath))
        {
            return ProjectRelativePath;
        }
    }

    if (FPaths::FileExists(ScriptPath))
    {
        return FPaths::ConvertRelativePathToFull(ScriptPath);
    }

    return FString();
}

FString BuildPythonExecutionCacheRoot_BP()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("PythonExec"));
}

FString ResolveIsolatedPythonExecutable_BP()
{
    FString ExecutablePath = FPlatformProcess::ExecutablePath();
#if PLATFORM_WINDOWS
    const FString ExecutableDir = FPaths::GetPath(ExecutablePath);
    const FString ExecutableName = FPaths::GetCleanFilename(ExecutablePath);
    if (ExecutableName.Equals(TEXT("UnrealEditor.exe"), ESearchCase::IgnoreCase))
    {
        const FString CmdExecutablePath = FPaths::Combine(ExecutableDir, TEXT("UnrealEditor.exe"));
        if (FPaths::FileExists(CmdExecutablePath))
        {
            return CmdExecutablePath;
        }
    }
#endif
    return ExecutablePath;
}

TArray<TSharedPtr<FJsonValue>> BuildProcessLogOutputArray_BP(const FString& ProcessOutput)
{
    TArray<TSharedPtr<FJsonValue>> LogArray;

    TArray<FString> Lines;
    ProcessOutput.ParseIntoArrayLines(Lines, false);
    for (const FString& Line : Lines)
    {
        FString Trimmed = Line;
        Trimmed.TrimStartAndEndInline();
        if (Trimmed.IsEmpty())
        {
            continue;
        }

        TSharedPtr<FJsonObject> LogEntryObj = MakeShared<FJsonObject>();
        LogEntryObj->SetStringField(TEXT("type"), TEXT("Info"));
        LogEntryObj->SetStringField(TEXT("output"), Trimmed);
        LogArray.Add(MakeShared<FJsonValueObject>(LogEntryObj));
    }

    return LogArray;
}

bool SplitBlueprintAssetReference_BP(
    const FString& BlueprintReference,
    FString& OutPackagePath,
    FString& OutObjectPath,
    FString& OutAssetName)
{
    FString Normalized = BlueprintReference;
    Normalized.TrimStartAndEndInline();
    if (Normalized.IsEmpty())
    {
        return false;
    }

    if (!Normalized.StartsWith(TEXT("/")))
    {
        Normalized = TEXT("/Game/Blueprints/") + Normalized;
    }

    OutPackagePath = Normalized.Contains(TEXT("."))
        ? FPackageName::ObjectPathToPackageName(Normalized)
        : Normalized;
    OutAssetName = FPackageName::GetLongPackageAssetName(OutPackagePath);
    if (OutAssetName.IsEmpty())
    {
        OutAssetName = FPaths::GetBaseFilename(OutPackagePath);
    }

    OutObjectPath = FString::Printf(TEXT("%s.%s"), *OutPackagePath, *OutAssetName);
    return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
}

bool ShouldSkipGraphNodeForPythonExport_BP(const UEdGraphNode* Node)
{
    return !Node || Node->GetClass()->GetName().Equals(TEXT("EdGraphNode_Comment"), ESearchCase::IgnoreCase);
}

FString SanitizeBlueprintGraphTextForPython_BP(const FString& RawText)
{
    TArray<FString> Lines;
    RawText.ParseIntoArrayLines(Lines, false);

    TArray<FString> SanitizedLines;
    SanitizedLines.Reserve(Lines.Num());

    bool bLastLineWasBlank = false;
    for (const FString& Line : Lines)
    {
        FString Trimmed = Line;
        Trimmed.TrimStartInline();

        const bool bSkipLine =
            Trimmed.StartsWith(TEXT("NodePosX=")) ||
            Trimmed.StartsWith(TEXT("NodePosY=")) ||
            Trimmed.StartsWith(TEXT("NodeWidth=")) ||
            Trimmed.StartsWith(TEXT("NodeHeight=")) ||
            Trimmed.StartsWith(TEXT("NodeComment=")) ||
            Trimmed.StartsWith(TEXT("bCommentBubbleVisible=")) ||
            Trimmed.StartsWith(TEXT("bCommentBubbleVisible_InDetailsPanel=")) ||
            Trimmed.StartsWith(TEXT("bCommentBubblePinned=")) ||
            Trimmed.StartsWith(TEXT("CommentColor=")) ||
            Trimmed.StartsWith(TEXT("ErrorType="));

        if (bSkipLine)
        {
            continue;
        }

        if (Trimmed.IsEmpty())
        {
            if (!bLastLineWasBlank)
            {
                SanitizedLines.Add(FString());
                bLastLineWasBlank = true;
            }
            continue;
        }

        SanitizedLines.Add(Line);
        bLastLineWasBlank = false;
    }

    return FString::Join(SanitizedLines, TEXT("\n"));
}

FString BuildRawGraphTextForPython_BP(UEdGraph* Graph)
{
    if (!Graph)
    {
        return FString();
    }

    TSet<UObject*> NodesToExport;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (ShouldSkipGraphNodeForPythonExport_BP(Node))
        {
            continue;
        }

        NodesToExport.Add(Node);
        Node->PrepareForCopying();
    }

    if (NodesToExport.IsEmpty())
    {
        return FString();
    }

    for (UObject* NodeObject : NodesToExport)
    {
        NodeObject->Mark(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));
    }

    FString RawText;
    FEdGraphUtilities::ExportNodesToText(NodesToExport, RawText);
    return SanitizeBlueprintGraphTextForPython_BP(RawText);
}

bool ShouldExportPropertyForPythonRoundTrip_BP(const FProperty* Property)
{
    if (!Property)
    {
        return false;
    }

    if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
    {
        return false;
    }

    return Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
}

TSharedPtr<FJsonObject> BuildPropertyMapForPythonRoundTrip_BP(
    UObject* SourceObject,
    UObject* BaselineObject,
    const TSet<FString>& ExcludedPropertyNames = TSet<FString>())
{
    TSharedPtr<FJsonObject> PropertyMap = MakeShared<FJsonObject>();
    if (!SourceObject)
    {
        return PropertyMap;
    }

    for (TFieldIterator<FProperty> It(SourceObject->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (!ShouldExportPropertyForPythonRoundTrip_BP(Property))
        {
            continue;
        }

        const FString NormalizedName = NormalizeLookupKey(Property->GetName());
        if (ExcludedPropertyNames.Contains(NormalizedName))
        {
            continue;
        }

        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(SourceObject);
        const void* BaselineValuePtr = nullptr;

        if (BaselineObject)
        {
            if (const FProperty* BaselineProperty = FindPropertyByNameLoose(BaselineObject->GetClass(), Property->GetName()))
            {
                BaselineValuePtr = BaselineProperty->ContainerPtrToValuePtr<void>(BaselineObject);
            }
        }

        if (BaselineValuePtr && Property->Identical(ValuePtr, BaselineValuePtr, PPF_None))
        {
            continue;
        }

        FString ValueText;
        PropertyMap->SetField(Property->GetName(), BuildPropertyValueJson_BP(Property, ValuePtr, ValueText));
    }

    return PropertyMap;
}

bool HasJsonFields_BP(const TSharedPtr<FJsonObject>& JsonObject)
{
    return JsonObject.IsValid() && JsonObject->Values.Num() > 0;
}

FString ExportPinTypeToPortableString_BP(const FEdGraphPinType& PinType)
{
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
    {
        return TEXT("bool");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
    {
        return TEXT("int");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
    {
        if (const UObject* EnumObject = PinType.PinSubCategoryObject.Get())
        {
            return EnumObject->GetPathName();
        }
        return TEXT("byte");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
    {
        return PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double ? TEXT("double") : TEXT("float");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
    {
        return TEXT("string");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
    {
        return TEXT("name");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
    {
        return TEXT("text");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
    {
        if (const UObject* StructObject = PinType.PinSubCategoryObject.Get())
        {
            if (StructObject == TBaseStructure<FVector>::Get())
            {
                return TEXT("vector");
            }
            if (StructObject == TBaseStructure<FRotator>::Get())
            {
                return TEXT("rotator");
            }
            return StructObject->GetPathName();
        }
        return TEXT("struct");
    }
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
        PinType.PinCategory == UEdGraphSchema_K2::PC_Interface ||
        PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
        PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
        PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
    {
        if (const UObject* TypeObject = PinType.PinSubCategoryObject.Get())
        {
            return TypeObject->GetPathName();
        }
        return TEXT("object");
    }

    return PinType.PinCategory.ToString();
}

TSharedPtr<FJsonObject> BuildBlueprintVariablePythonSpec_BP(const FBPVariableDescription& Variable)
{
    TSharedPtr<FJsonObject> VariableSpec = MakeShared<FJsonObject>();
    VariableSpec->SetStringField(TEXT("name"), Variable.VarName.ToString());
    VariableSpec->SetStringField(TEXT("variable_name"), Variable.VarName.ToString());
    VariableSpec->SetStringField(TEXT("variable_type"), ExportPinTypeToPortableString_BP(Variable.VarType));
    VariableSpec->SetStringField(TEXT("default_value"), Variable.DefaultValue);
    VariableSpec->SetStringField(TEXT("friendly_name"), Variable.FriendlyName.IsEmpty() ? Variable.VarName.ToString() : Variable.FriendlyName);
    VariableSpec->SetStringField(TEXT("category"), Variable.Category.ToString());

    FString Tooltip;
    if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
    {
        Tooltip = Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip);
    }
    VariableSpec->SetStringField(TEXT("tooltip"), Tooltip);
    VariableSpec->SetObjectField(TEXT("specifiers"), BuildVariableSpecifierJson_BP(Variable));
    return VariableSpec;
}

FString SerializeJsonPretty_BP(const TSharedPtr<FJsonObject>& JsonObject)
{
    if (!JsonObject.IsValid())
    {
        return TEXT("{}");
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    return Output;
}

FString SerializeJsonArrayPretty_BP(const TArray<TSharedPtr<FJsonValue>>& JsonArray)
{
    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(JsonArray, Writer);
    return Output;
}

FString MakePythonMultilineStringLiteral_BP(const FString& Text)
{
    if (!Text.Contains(TEXT("'''")))
    {
        return FString::Printf(TEXT("r'''%s'''"), *Text);
    }

    if (!Text.Contains(TEXT("\"\"\"")))
    {
        return FString::Printf(TEXT("r\"\"\"%s\"\"\""), *Text);
    }

    FString Escaped = Text;
    Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Escaped.ReplaceInline(TEXT("'''"), TEXT("\\'\\'\\'"));
    return FString::Printf(TEXT("'''%s'''"), *Escaped);
}

TSharedPtr<FJsonValue> BuildJsonVectorValue_BP(const FVector& Value)
{
    TArray<TSharedPtr<FJsonValue>> Elements;
    Elements.Add(MakeShared<FJsonValueNumber>(Value.X));
    Elements.Add(MakeShared<FJsonValueNumber>(Value.Y));
    Elements.Add(MakeShared<FJsonValueNumber>(Value.Z));
    return MakeShared<FJsonValueArray>(Elements);
}

TSharedPtr<FJsonValue> BuildJsonRotatorValue_BP(const FRotator& Value)
{
    TArray<TSharedPtr<FJsonValue>> Elements;
    Elements.Add(MakeShared<FJsonValueNumber>(Value.Pitch));
    Elements.Add(MakeShared<FJsonValueNumber>(Value.Yaw));
    Elements.Add(MakeShared<FJsonValueNumber>(Value.Roll));
    return MakeShared<FJsonValueArray>(Elements);
}

FString MakePortableKeySlug_BP(const FString& Input)
{
    FString Slug;
    Slug.Reserve(Input.Len());

    for (const TCHAR Char : Input)
    {
        if (FChar::IsAlnum(Char))
        {
            Slug.AppendChar(FChar::ToLower(Char));
        }
        else if (Slug.IsEmpty() || !Slug.EndsWith(TEXT("_")))
        {
            Slug.AppendChar(TEXT('_'));
        }
    }

    Slug.TrimStartAndEndInline();
    while (Slug.RemoveFromEnd(TEXT("_")))
    {
    }

    return Slug.IsEmpty() ? TEXT("graph") : Slug;
}

UActorComponent* FindActorComponentByName_BP(AActor* Actor, const FString& ComponentName)
{
    if (!Actor || ComponentName.IsEmpty())
    {
        return nullptr;
    }

    TArray<UActorComponent*> Components;
    Actor->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
        {
            return Component;
        }
    }

    return nullptr;
}

void AppendSceneComponentTransformFields_BP(
    const USceneComponent* SourceComponent,
    const USceneComponent* BaselineComponent,
    const TSharedPtr<FJsonObject>& ComponentSpec)
{
    if (!SourceComponent || !ComponentSpec.IsValid())
    {
        return;
    }

    const FVector BaselineLocation = BaselineComponent ? BaselineComponent->GetRelativeLocation() : FVector::ZeroVector;
    const FRotator BaselineRotation = BaselineComponent ? BaselineComponent->GetRelativeRotation() : FRotator::ZeroRotator;
    const FVector BaselineScale = BaselineComponent ? BaselineComponent->GetRelativeScale3D() : FVector(1.0, 1.0, 1.0);

    if (!SourceComponent->GetRelativeLocation().Equals(BaselineLocation, KINDA_SMALL_NUMBER))
    {
        ComponentSpec->SetField(TEXT("location"), BuildJsonVectorValue_BP(SourceComponent->GetRelativeLocation()));
    }

    if (!SourceComponent->GetRelativeRotation().Equals(BaselineRotation, KINDA_SMALL_NUMBER))
    {
        ComponentSpec->SetField(TEXT("rotation"), BuildJsonRotatorValue_BP(SourceComponent->GetRelativeRotation()));
    }

    if (!SourceComponent->GetRelativeScale3D().Equals(BaselineScale, KINDA_SMALL_NUMBER))
    {
        ComponentSpec->SetField(TEXT("scale"), BuildJsonVectorValue_BP(SourceComponent->GetRelativeScale3D()));
    }
}

TSharedPtr<FJsonObject> BuildBlueprintDefaultPropertyMapForPython_BP(UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return MakeShared<FJsonObject>();
    }

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(Blueprint);
    UObject* BlueprintCDO = BlueprintClass ? BlueprintClass->GetDefaultObject() : nullptr;
    UObject* ParentCDO = Blueprint->ParentClass ? Blueprint->ParentClass->GetDefaultObject() : nullptr;
    if (!BlueprintCDO)
    {
        return MakeShared<FJsonObject>();
    }

    TSet<FString> ExcludedPropertyNames;
    for (TFieldIterator<FProperty> It(BlueprintCDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*It))
        {
            if (ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
            {
                ExcludedPropertyNames.Add(NormalizeLookupKey(ObjectProperty->GetName()));
            }
        }
    }

    return BuildPropertyMapForPythonRoundTrip_BP(BlueprintCDO, ParentCDO, ExcludedPropertyNames);
}

void AppendPythonComponentSpecs_BP(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutComponents)
{
    OutComponents.Reset();
    if (!Blueprint)
    {
        return;
    }

    TSet<FString> CreatedComponentNames;
    const TSet<FString> PropertyExclusions = {
        NormalizeLookupKey(TEXT("RelativeLocation")),
        NormalizeLookupKey(TEXT("RelativeRotation")),
        NormalizeLookupKey(TEXT("RelativeScale3D")),
        NormalizeLookupKey(TEXT("AttachParent")),
        NormalizeLookupKey(TEXT("AttachChildren"))
    };

    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (!Node || !Node->ComponentTemplate)
            {
                continue;
            }

            const FString ComponentName = Node->GetVariableName().ToString();
            CreatedComponentNames.Add(NormalizeLookupKey(ComponentName));

            TSharedPtr<FJsonObject> ComponentSpec = MakeShared<FJsonObject>();
            ComponentSpec->SetStringField(TEXT("operation"), TEXT("create"));
            ComponentSpec->SetStringField(TEXT("component_name"), ComponentName);
            ComponentSpec->SetStringField(
                TEXT("component_type"),
                Node->ComponentClass ? Node->ComponentClass->GetPathName() : Node->ComponentTemplate->GetClass()->GetPathName());

            if (Node->ParentComponentOrVariableName != NAME_None)
            {
                ComponentSpec->SetStringField(TEXT("parent_component_name"), Node->ParentComponentOrVariableName.ToString());
            }

            if (Node->AttachToName != NAME_None)
            {
                ComponentSpec->SetStringField(TEXT("attach_to_name"), Node->AttachToName.ToString());
            }

            UActorComponent* BaselineComponent = Node->ComponentClass
                ? Cast<UActorComponent>(Node->ComponentClass->GetDefaultObject())
                : nullptr;

            if (const USceneComponent* SceneComponent = Cast<USceneComponent>(Node->ComponentTemplate))
            {
                AppendSceneComponentTransformFields_BP(
                    SceneComponent,
                    Cast<USceneComponent>(BaselineComponent),
                    ComponentSpec);
            }

            const TSharedPtr<FJsonObject> PropertyMap =
                BuildPropertyMapForPythonRoundTrip_BP(Node->ComponentTemplate, BaselineComponent, PropertyExclusions);
            if (HasJsonFields_BP(PropertyMap))
            {
                ComponentSpec->SetObjectField(TEXT("component_properties"), PropertyMap);
            }

            OutComponents.Add(MakeShared<FJsonValueObject>(ComponentSpec));
        }
    }

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(Blueprint);
    AActor* BlueprintCDO = BlueprintClass ? Cast<AActor>(BlueprintClass->GetDefaultObject()) : nullptr;
    AActor* ParentCDO = Blueprint->ParentClass ? Cast<AActor>(Blueprint->ParentClass->GetDefaultObject()) : nullptr;
    if (!BlueprintCDO)
    {
        return;
    }

    TArray<UActorComponent*> ChildComponents;
    BlueprintCDO->GetComponents(ChildComponents);
    for (UActorComponent* ChildComponent : ChildComponents)
    {
        if (!ChildComponent)
        {
            continue;
        }

        const FString ComponentName = ChildComponent->GetName();
        if (CreatedComponentNames.Contains(NormalizeLookupKey(ComponentName)))
        {
            continue;
        }

        UActorComponent* BaselineComponent = FindActorComponentByName_BP(ParentCDO, ComponentName);
        if (!BaselineComponent)
        {
            BaselineComponent = Cast<UActorComponent>(ChildComponent->GetClass()->GetDefaultObject());
        }

        TSharedPtr<FJsonObject> ComponentSpec = MakeShared<FJsonObject>();
        ComponentSpec->SetStringField(TEXT("operation"), TEXT("edit"));
        ComponentSpec->SetStringField(TEXT("component_name"), ComponentName);
        ComponentSpec->SetStringField(TEXT("component_type"), ChildComponent->GetClass()->GetPathName());

        if (const USceneComponent* SceneComponent = Cast<USceneComponent>(ChildComponent))
        {
            AppendSceneComponentTransformFields_BP(
                SceneComponent,
                Cast<USceneComponent>(BaselineComponent),
                ComponentSpec);
        }

        const TSharedPtr<FJsonObject> PropertyMap =
            BuildPropertyMapForPythonRoundTrip_BP(ChildComponent, BaselineComponent, PropertyExclusions);
        if (HasJsonFields_BP(PropertyMap))
        {
            ComponentSpec->SetObjectField(TEXT("component_properties"), PropertyMap);
        }

        if (ComponentSpec->Values.Num() > 3)
        {
            OutComponents.Add(MakeShared<FJsonValueObject>(ComponentSpec));
        }
    }
}

void AppendPythonGraphSpecs_BP(
    UBlueprint* Blueprint,
    TArray<TSharedPtr<FJsonValue>>& OutGraphs,
    TMap<FString, FString>& OutGraphTexts)
{
    OutGraphs.Reset();
    OutGraphTexts.Reset();
    if (!Blueprint)
    {
        return;
    }

    auto AddGraphSpecs = [&OutGraphs, &OutGraphTexts](const TArray<UEdGraph*>& Graphs, const TCHAR* GraphKind)
    {
        for (int32 Index = 0; Index < Graphs.Num(); ++Index)
        {
            UEdGraph* Graph = Graphs[Index];
            if (!Graph)
            {
                continue;
            }

            const FString RawText = BuildRawGraphTextForPython_BP(Graph);
            if (RawText.IsEmpty())
            {
                continue;
            }

            const FString TextKey = FString::Printf(TEXT("%s_%03d_%s"), GraphKind, Index, *MakePortableKeySlug_BP(Graph->GetName()));
            OutGraphTexts.Add(TextKey, RawText);

            TSharedPtr<FJsonObject> GraphSpec = MakeShared<FJsonObject>();
            GraphSpec->SetStringField(TEXT("graph_name"), Graph->GetName());
            GraphSpec->SetStringField(TEXT("graph_kind"), GraphKind);
            GraphSpec->SetStringField(TEXT("text_key"), TextKey);
            GraphSpec->SetBoolField(TEXT("clear_graph"), true);
            OutGraphs.Add(MakeShared<FJsonValueObject>(GraphSpec));
        }
    };

    AddGraphSpecs(Blueprint->UbergraphPages, TEXT("ubergraph"));
    AddGraphSpecs(Blueprint->FunctionGraphs, TEXT("function"));
    AddGraphSpecs(Blueprint->MacroGraphs, TEXT("macro"));
}

TSharedPtr<FJsonObject> BuildBlueprintPythonSpec_BP(UBlueprint* Blueprint, TMap<FString, FString>& OutGraphTexts)
{
    TSharedPtr<FJsonObject> Spec = MakeShared<FJsonObject>();
    OutGraphTexts.Reset();

    if (!Blueprint)
    {
        return Spec;
    }

    FString PackagePath;
    FString ObjectPath;
    FString AssetName;
    SplitBlueprintAssetReference_BP(Blueprint->GetPathName(), PackagePath, ObjectPath, AssetName);

    Spec->SetNumberField(TEXT("schema"), 1);
    Spec->SetNumberField(TEXT("format_version"), 1);
    Spec->SetStringField(TEXT("path"), ObjectPath);
    Spec->SetStringField(TEXT("asset_name"), AssetName);
    Spec->SetStringField(TEXT("package_path"), PackagePath);
    Spec->SetStringField(TEXT("object_path"), ObjectPath);
    Spec->SetStringField(TEXT("source_blueprint"), Blueprint->GetPathName());
    Spec->SetStringField(TEXT("target_blueprint"), Blueprint->GetPathName());
    Spec->SetStringField(TEXT("bp_type"), TEXT("Normal"));
    Spec->SetStringField(TEXT("parent"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT(""));
    Spec->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT(""));
    Spec->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT(""));

    const TSharedPtr<FJsonObject> BlueprintProperties = BuildBlueprintDefaultPropertyMapForPython_BP(Blueprint);
    if (HasJsonFields_BP(BlueprintProperties))
    {
        Spec->SetObjectField(TEXT("blueprint_properties"), BlueprintProperties);
    }

    TArray<TSharedPtr<FJsonValue>> ComponentSpecs;
    AppendPythonComponentSpecs_BP(Blueprint, ComponentSpecs);
    Spec->SetArrayField(TEXT("components"), ComponentSpecs);

    TArray<TSharedPtr<FJsonValue>> VariableSpecs;
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        VariableSpecs.Add(MakeShared<FJsonValueObject>(BuildBlueprintVariablePythonSpec_BP(Variable)));
    }
    VariableSpecs.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
    {
        const TSharedPtr<FJsonObject> AObj = A.IsValid() ? A->AsObject() : nullptr;
        const TSharedPtr<FJsonObject> BObj = B.IsValid() ? B->AsObject() : nullptr;
        const FString AName = AObj.IsValid() ? AObj->GetStringField(TEXT("variable_name")) : FString();
        const FString BName = BObj.IsValid() ? BObj->GetStringField(TEXT("variable_name")) : FString();
        return AName < BName;
    });
    Spec->SetArrayField(TEXT("variables"), VariableSpecs);

    TArray<TSharedPtr<FJsonValue>> InterfaceSpecs;
    for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
    {
        if (InterfaceDescription.Interface)
        {
            InterfaceSpecs.Add(MakeShared<FJsonValueString>(InterfaceDescription.Interface->GetClassPathName().ToString()));
        }
    }
    InterfaceSpecs.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
    {
        FString AValue;
        FString BValue;
        const bool bHasA = A.IsValid() && A->TryGetString(AValue);
        const bool bHasB = B.IsValid() && B->TryGetString(BValue);
        return bHasA && (!bHasB || AValue < BValue);
    });
    Spec->SetArrayField(TEXT("interfaces"), InterfaceSpecs);

    TArray<TSharedPtr<FJsonValue>> GraphSpecs;
    AppendPythonGraphSpecs_BP(Blueprint, GraphSpecs, OutGraphTexts);
    Spec->SetArrayField(TEXT("graphs"), GraphSpecs);

    return Spec;
}

FString BuildBlueprintPythonScript_BP(UBlueprint* Blueprint)
{
    TMap<FString, FString> GraphTexts;
    const TSharedPtr<FJsonObject> Spec = BuildBlueprintPythonSpec_BP(Blueprint, GraphTexts);

    TSharedPtr<FJsonObject> InfoObject = MakeShared<FJsonObject>();
    const TArray<FString> InfoFields = {
        TEXT("schema"), TEXT("format_version"), TEXT("path"), TEXT("asset_name"),
        TEXT("package_path"), TEXT("object_path"), TEXT("source_blueprint"),
        TEXT("target_blueprint"), TEXT("bp_type"), TEXT("parent"), TEXT("parent_class"),
        TEXT("parent_class_path")
    };
    for (const FString& FieldName : InfoFields)
    {
        if (const TSharedPtr<FJsonValue>* Value = Spec->Values.Find(FieldName))
        {
            InfoObject->SetField(FieldName, *Value);
        }
    }

    const TSharedPtr<FJsonObject>* BlueprintProperties = nullptr;
    const bool bHasBlueprintProperties =
        Spec->TryGetObjectField(TEXT("blueprint_properties"), BlueprintProperties) && BlueprintProperties && BlueprintProperties->IsValid();

    const TArray<TSharedPtr<FJsonValue>>* Interfaces = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
    Spec->TryGetArrayField(TEXT("interfaces"), Interfaces);
    Spec->TryGetArrayField(TEXT("variables"), Variables);
    Spec->TryGetArrayField(TEXT("components"), Components);
    Spec->TryGetArrayField(TEXT("graphs"), Graphs);

    FString BlueprintPropertiesJson = TEXT("{}");
    if (bHasBlueprintProperties)
    {
        BlueprintPropertiesJson = SerializeJsonPretty_BP(*BlueprintProperties);
    }

    FString InterfacesJson = TEXT("[]");
    if (Interfaces)
    {
        InterfacesJson = SerializeJsonArrayPretty_BP(*Interfaces);
    }

    FString VariablesJson = TEXT("[]");
    if (Variables)
    {
        VariablesJson = SerializeJsonArrayPretty_BP(*Variables);
    }

    FString ComponentsJson = TEXT("[]");
    if (Components)
    {
        ComponentsJson = SerializeJsonArrayPretty_BP(*Components);
    }

    FString GraphsJson = TEXT("[]");
    if (Graphs)
    {
        GraphsJson = SerializeJsonArrayPretty_BP(*Graphs);
    }

    TArray<FString> Lines;
    Lines.Add(TEXT("# -*- coding: utf-8 -*-"));
    Lines.Add(TEXT("\"\"\""));
    Lines.Add(TEXT("UE Blueprint Export"));
    Lines.Add(FString::Printf(TEXT("Path:    %s"), Blueprint ? *Blueprint->GetPathName() : TEXT("")));
    Lines.Add(FString::Printf(TEXT("Parent:  %s"), Blueprint && Blueprint->ParentClass ? *Blueprint->ParentClass->GetPathName() : TEXT("")));
    Lines.Add(TEXT("Type:    BlueprintNormal"));
    Lines.Add(TEXT("Schema:  1"));
    Lines.Add(TEXT("\"\"\""));
    Lines.Add(TEXT("import json"));
    Lines.Add(TEXT("import unreal"));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("# Blueprint metadata"));
    Lines.Add(FString::Printf(TEXT("_INFO = json.loads(%s)"), *MakePythonMultilineStringLiteral_BP(SerializeJsonPretty_BP(InfoObject))));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("# Default object property overrides"));
    Lines.Add(FString::Printf(TEXT("_BLUEPRINT_PROPERTIES = json.loads(%s)"), *MakePythonMultilineStringLiteral_BP(BlueprintPropertiesJson)));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("# Interfaces"));
    Lines.Add(FString::Printf(TEXT("_INTERFACES = json.loads(%s)"), *MakePythonMultilineStringLiteral_BP(InterfacesJson)));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("# Variables"));
    Lines.Add(FString::Printf(TEXT("_VARIABLES = json.loads(%s)"), *MakePythonMultilineStringLiteral_BP(VariablesJson)));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("# Components"));
    Lines.Add(FString::Printf(TEXT("_COMPONENTS = json.loads(%s)"), *MakePythonMultilineStringLiteral_BP(ComponentsJson)));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("# Graph descriptors"));
    Lines.Add(FString::Printf(TEXT("_GRAPHS = json.loads(%s)"), *MakePythonMultilineStringLiteral_BP(GraphsJson)));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("# Raw graph payloads used for exact round-trip import"));
    Lines.Add(TEXT("_GRAPH_TEXTS = {"));

    TArray<FString> GraphKeys;
    GraphTexts.GetKeys(GraphKeys);
    GraphKeys.Sort();
    for (const FString& GraphKey : GraphKeys)
    {
        const FString* GraphText = GraphTexts.Find(GraphKey);
        if (!GraphText)
        {
            continue;
        }

        Lines.Add(FString::Printf(
            TEXT("    %s: %s,"),
            *MakePythonMultilineStringLiteral_BP(GraphKey),
            *MakePythonMultilineStringLiteral_BP(*GraphText)));
    }

    Lines.Add(TEXT("}"));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("def _materialize_spec():"));
    Lines.Add(TEXT("    spec = dict(_INFO)"));
    Lines.Add(TEXT("    if _BLUEPRINT_PROPERTIES:"));
    Lines.Add(TEXT("        spec[\"blueprint_properties\"] = dict(_BLUEPRINT_PROPERTIES)"));
    Lines.Add(TEXT("    spec[\"interfaces\"] = list(_INTERFACES)"));
    Lines.Add(TEXT("    spec[\"variables\"] = list(_VARIABLES)"));
    Lines.Add(TEXT("    spec[\"components\"] = list(_COMPONENTS)"));
    Lines.Add(TEXT("    spec[\"graphs\"] = [dict(graph) for graph in _GRAPHS]"));
    Lines.Add(TEXT("    for graph in spec[\"graphs\"]:"));
    Lines.Add(TEXT("        text_key = graph.get(\"text_key\")"));
    Lines.Add(TEXT("        if text_key:"));
    Lines.Add(TEXT("            graph[\"node_text\"] = _GRAPH_TEXTS.get(text_key, \"\")"));
    Lines.Add(TEXT("    return spec"));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("def restore(target=None, overwrite=True, compile_blueprint=True):"));
    Lines.Add(TEXT("    bridge = unreal.get_editor_subsystem(unreal.EpicUnrealMCPBridge)"));
    Lines.Add(TEXT("    if bridge is None:"));
    Lines.Add(TEXT("        raise RuntimeError(\"EpicUnrealMCPBridge editor subsystem is unavailable\")"));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("    spec = _materialize_spec()"));
    Lines.Add(TEXT("    if target:"));
    Lines.Add(TEXT("        spec[\"target_blueprint\"] = target"));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("    payload = {"));
    Lines.Add(TEXT("        \"spec\": spec,"));
    Lines.Add(TEXT("        \"overwrite\": overwrite,"));
    Lines.Add(TEXT("        \"compile_blueprint\": compile_blueprint,"));
    Lines.Add(TEXT("    }"));
    Lines.Add(TEXT("    result_text = bridge.execute_command_json(\"import_blueprint_py\", json.dumps(payload, ensure_ascii=False))"));
    Lines.Add(TEXT("    result = json.loads(result_text)"));
    Lines.Add(TEXT("    if result.get(\"status\") != \"success\":"));
    Lines.Add(TEXT("        raise RuntimeError(result.get(\"error\", \"import_blueprint_py failed\"))"));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("    inner = result.get(\"result\", {})"));
    Lines.Add(TEXT("    if not inner.get(\"success\", False):"));
    Lines.Add(TEXT("        raise RuntimeError(inner.get(\"error\", \"import_blueprint_py failed\"))"));
    Lines.Add(TEXT("    return inner"));
    Lines.Add(TEXT(""));
    Lines.Add(TEXT("if __name__ == \"__main__\":"));
    Lines.Add(TEXT("    print(json.dumps(restore(), ensure_ascii=False, indent=2))"));

    return FString::Join(Lines, TEXT("\n"));
}

void SkipPythonWhitespace_BP(const FString& Text, int32& InOutIndex)
{
    while (InOutIndex < Text.Len() && FChar::IsWhitespace(Text[InOutIndex]))
    {
        ++InOutIndex;
    }
}

bool ParseGeneratedPythonStringLiteral_BP(const FString& Text, int32 StartIndex, FString& OutValue, int32& OutNextIndex)
{
    OutValue.Reset();
    OutNextIndex = StartIndex;

    int32 Index = StartIndex;
    SkipPythonWhitespace_BP(Text, Index);

    bool bRawString = false;
    if (Index < Text.Len() && (Text[Index] == TEXT('r') || Text[Index] == TEXT('R')))
    {
        const bool bHasSingleQuoteDelimiter =
            Index + 3 < Text.Len() && Text.Mid(Index + 1, 3) == TEXT("'''");
        const bool bHasDoubleQuoteDelimiter =
            Index + 3 < Text.Len() && Text.Mid(Index + 1, 3) == TEXT("\"\"\"");
        if (bHasSingleQuoteDelimiter || bHasDoubleQuoteDelimiter)
        {
            bRawString = true;
            ++Index;
        }
    }

    if (Index + 2 >= Text.Len())
    {
        return false;
    }

    const FString Delimiter = Text.Mid(Index, 3);
    if (Delimiter != TEXT("'''") && Delimiter != TEXT("\"\"\""))
    {
        return false;
    }

    const int32 ContentStart = Index + 3;
    const int32 ContentEnd = Text.Find(Delimiter, ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
    if (ContentEnd == INDEX_NONE)
    {
        return false;
    }

    OutValue = Text.Mid(ContentStart, ContentEnd - ContentStart);
    if (!bRawString)
    {
        OutValue.ReplaceInline(TEXT("\\'\\'\\'"), TEXT("'''"));
        OutValue.ReplaceInline(TEXT("\\\\"),
            TEXT("\\"));
    }

    OutNextIndex = ContentEnd + 3;
    return true;
}

bool ExtractGeneratedPythonJsonSection_BP(const FString& ScriptText, const FString& Marker, FString& OutJsonText)
{
    const int32 MarkerIndex = ScriptText.Find(Marker, ESearchCase::CaseSensitive);
    if (MarkerIndex == INDEX_NONE)
    {
        return false;
    }

    int32 NextIndex = INDEX_NONE;
    return ParseGeneratedPythonStringLiteral_BP(ScriptText, MarkerIndex + Marker.Len(), OutJsonText, NextIndex);
}

bool ParseGeneratedPythonGraphTextMap_BP(const FString& ScriptText, TMap<FString, FString>& OutGraphTexts)
{
    OutGraphTexts.Reset();

    const FString Marker = TEXT("_GRAPH_TEXTS = {");
    const int32 MarkerIndex = ScriptText.Find(Marker, ESearchCase::CaseSensitive);
    if (MarkerIndex == INDEX_NONE)
    {
        return false;
    }

    int32 Index = MarkerIndex + Marker.Len();
    while (Index < ScriptText.Len())
    {
        SkipPythonWhitespace_BP(ScriptText, Index);
        if (Index >= ScriptText.Len())
        {
            break;
        }

        if (ScriptText[Index] == TEXT('}'))
        {
            return true;
        }

        FString Key;
        if (!ParseGeneratedPythonStringLiteral_BP(ScriptText, Index, Key, Index))
        {
            return false;
        }

        SkipPythonWhitespace_BP(ScriptText, Index);
        if (Index >= ScriptText.Len() || ScriptText[Index] != TEXT(':'))
        {
            return false;
        }
        ++Index;

        FString Value;
        if (!ParseGeneratedPythonStringLiteral_BP(ScriptText, Index, Value, Index))
        {
            return false;
        }

        OutGraphTexts.Add(Key, Value);

        SkipPythonWhitespace_BP(ScriptText, Index);
        if (Index < ScriptText.Len() && ScriptText[Index] == TEXT(','))
        {
            ++Index;
        }
    }

    return false;
}

bool ExtractPythonImportSpec_BP(
    const TSharedPtr<FJsonObject>& Params,
    TSharedPtr<FJsonObject>& OutSpec,
    FString& OutError)
{
    OutSpec.Reset();
    OutError.Reset();

    if (!Params.IsValid())
    {
        OutError = TEXT("Invalid params object");
        return false;
    }

    const TSharedPtr<FJsonObject>* SpecObject = nullptr;
    if (Params->TryGetObjectField(TEXT("spec"), SpecObject) && SpecObject && SpecObject->IsValid())
    {
        OutSpec = *SpecObject;
        return true;
    }

    FString SpecJson;
    if (Params->TryGetStringField(TEXT("spec_json"), SpecJson))
    {
        if (!TryDeserializeJsonObject_BP(SpecJson, OutSpec))
        {
            OutError = TEXT("Failed to parse 'spec_json' as a JSON object");
            return false;
        }

        return true;
    }

    FString ScriptText;
    Params->TryGetStringField(TEXT("python_text"), ScriptText);
    if (ScriptText.IsEmpty())
    {
        Params->TryGetStringField(TEXT("script_text"), ScriptText);
    }

    FString InputPath;
    if (ScriptText.IsEmpty())
    {
        Params->TryGetStringField(TEXT("input_path"), InputPath);
        if (InputPath.IsEmpty())
        {
            Params->TryGetStringField(TEXT("script_path"), InputPath);
        }

        if (!InputPath.IsEmpty())
        {
            if (FPaths::IsRelative(InputPath))
            {
                InputPath = FPaths::Combine(FPaths::ProjectDir(), InputPath);
            }

            if (!FFileHelper::LoadFileToString(ScriptText, *InputPath))
            {
                OutError = FString::Printf(TEXT("Failed to read python export file: %s"), *InputPath);
                return false;
            }
        }
    }

    if (!ScriptText.IsEmpty())
    {
        FString InfoJson;
        FString BlueprintPropertiesJson;
        FString InterfacesJson;
        FString VariablesJson;
        FString ComponentsJson;
        FString GraphsJson;

        if (!ExtractGeneratedPythonJsonSection_BP(ScriptText, TEXT("_INFO = json.loads("), InfoJson) ||
            !TryDeserializeJsonObject_BP(InfoJson, OutSpec))
        {
            OutError = TEXT("Failed to parse _INFO section from python export");
            return false;
        }

        ExtractGeneratedPythonJsonSection_BP(ScriptText, TEXT("_BLUEPRINT_PROPERTIES = json.loads("), BlueprintPropertiesJson);
        ExtractGeneratedPythonJsonSection_BP(ScriptText, TEXT("_INTERFACES = json.loads("), InterfacesJson);
        ExtractGeneratedPythonJsonSection_BP(ScriptText, TEXT("_VARIABLES = json.loads("), VariablesJson);
        ExtractGeneratedPythonJsonSection_BP(ScriptText, TEXT("_COMPONENTS = json.loads("), ComponentsJson);
        ExtractGeneratedPythonJsonSection_BP(ScriptText, TEXT("_GRAPHS = json.loads("), GraphsJson);

        TSharedPtr<FJsonObject> BlueprintPropertiesObject;
        if (!BlueprintPropertiesJson.IsEmpty() &&
            TryDeserializeJsonObject_BP(BlueprintPropertiesJson, BlueprintPropertiesObject) &&
            BlueprintPropertiesObject.IsValid() &&
            BlueprintPropertiesObject->Values.Num() > 0)
        {
            OutSpec->SetObjectField(TEXT("blueprint_properties"), BlueprintPropertiesObject);
        }

        TArray<TSharedPtr<FJsonValue>> InterfacesArray;
        if (!InterfacesJson.IsEmpty() && TryDeserializeJsonArray_BP(InterfacesJson, InterfacesArray))
        {
            OutSpec->SetArrayField(TEXT("interfaces"), InterfacesArray);
        }

        TArray<TSharedPtr<FJsonValue>> VariablesArray;
        if (!VariablesJson.IsEmpty() && TryDeserializeJsonArray_BP(VariablesJson, VariablesArray))
        {
            OutSpec->SetArrayField(TEXT("variables"), VariablesArray);
        }

        TArray<TSharedPtr<FJsonValue>> ComponentsArray;
        if (!ComponentsJson.IsEmpty() && TryDeserializeJsonArray_BP(ComponentsJson, ComponentsArray))
        {
            OutSpec->SetArrayField(TEXT("components"), ComponentsArray);
        }

        TArray<TSharedPtr<FJsonValue>> GraphsArray;
        if (!GraphsJson.IsEmpty() && TryDeserializeJsonArray_BP(GraphsJson, GraphsArray))
        {
            TMap<FString, FString> GraphTexts;
            if (!ParseGeneratedPythonGraphTextMap_BP(ScriptText, GraphTexts))
            {
                OutError = TEXT("Failed to parse _GRAPH_TEXTS section from python export");
                return false;
            }

            for (const TSharedPtr<FJsonValue>& GraphValue : GraphsArray)
            {
                if (!GraphValue.IsValid() || GraphValue->Type != EJson::Object)
                {
                    continue;
                }

                const TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
                FString TextKey;
                if (GraphObject.IsValid() && GraphObject->TryGetStringField(TEXT("text_key"), TextKey))
                {
                    if (const FString* GraphText = GraphTexts.Find(TextKey))
                    {
                        GraphObject->SetStringField(TEXT("node_text"), *GraphText);
                    }
                }
            }

            OutSpec->SetArrayField(TEXT("graphs"), GraphsArray);
        }

        return true;
    }

    OutError = TEXT("Missing 'spec', 'spec_json', 'python_text', or 'input_path' parameter");
    return false;
}

UClass* ResolveParentClassForPythonImport_BP(const TSharedPtr<FJsonObject>& Spec)
{
    if (!Spec.IsValid())
    {
        return AActor::StaticClass();
    }

    FString ParentClassRef;
    if (!Spec->TryGetStringField(TEXT("parent_class_path"), ParentClassRef) || ParentClassRef.IsEmpty())
    {
        if (!Spec->TryGetStringField(TEXT("parent"), ParentClassRef) || ParentClassRef.IsEmpty())
        {
            Spec->TryGetStringField(TEXT("parent_class"), ParentClassRef);
        }
    }

    if (UClass* ParentClass = ResolveClassByReference(ParentClassRef))
    {
        return ParentClass;
    }

    return AActor::StaticClass();
}

UBlueprint* CreateBlueprintAssetForPythonImport_BP(const FString& BlueprintReference, UClass* ParentClass, FString& OutError)
{
    OutError.Reset();

    FString PackagePath;
    FString ObjectPath;
    FString AssetName;
    if (!SplitBlueprintAssetReference_BP(BlueprintReference, PackagePath, ObjectPath, AssetName))
    {
        OutError = FString::Printf(TEXT("Invalid target blueprint path: %s"), *BlueprintReference);
        return nullptr;
    }

    if (!PackagePath.StartsWith(TEXT("/Game/")))
    {
        OutError = FString::Printf(TEXT("Target blueprint must be under /Game: %s"), *PackagePath);
        return nullptr;
    }

    if (UEditorAssetLibrary::DoesAssetExist(ObjectPath))
    {
        return FEpicUnrealMCPCommonUtils::FindBlueprint(ObjectPath);
    }

    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackagePath);
        return nullptr;
    }

    UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass ? ParentClass : AActor::StaticClass(),
        Package,
        FName(*AssetName),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(),
        FName(TEXT("UnrealMCP")));

    if (!NewBlueprint)
    {
        OutError = FString::Printf(TEXT("Failed to create blueprint asset: %s"), *ObjectPath);
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(NewBlueprint);
    Package->MarkPackageDirty();
    return NewBlueprint;
}

void ApplyInterfacesForPythonImport_BP(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Spec)
{
    if (!Blueprint || !Spec.IsValid())
    {
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Interfaces = nullptr;
    if (!Spec->TryGetArrayField(TEXT("interfaces"), Interfaces) || !Interfaces)
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& InterfaceValue : *Interfaces)
    {
        FString InterfaceRef;
        if (!TryGetJsonValueAsString_BP(InterfaceValue, InterfaceRef) || InterfaceRef.IsEmpty())
        {
            continue;
        }

        UClass* InterfaceClass = ResolveClassByReference(InterfaceRef);
        if (!InterfaceClass)
        {
            continue;
        }

        const bool bAlreadyImplemented = Blueprint->ImplementedInterfaces.ContainsByPredicate(
            [InterfaceClass](const FBPInterfaceDescription& Description)
            {
                return Description.Interface == InterfaceClass;
            });

        if (!bAlreadyImplemented)
        {
            FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName());
        }
    }

    FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);
}

UEdGraph* ResolveGraphForPythonImport_BP(UBlueprint* Blueprint, const FString& GraphName, const FString& GraphKind)
{
    if (!Blueprint || GraphName.IsEmpty())
    {
        return nullptr;
    }

    if (UEdGraph* ExistingGraph = FindBlueprintGraphByNameOrPath(Blueprint, GraphName))
    {
        return ExistingGraph;
    }

    if (GraphKind.Equals(TEXT("function"), ESearchCase::IgnoreCase))
    {
        UExportBlueprintToTxtLibrary::EnsureFunctionGraph(Blueprint, nullptr, GraphName);
        return FindBlueprintGraphByNameOrPath(Blueprint, GraphName);
    }

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint,
        FName(*GraphName),
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());

    if (!NewGraph)
    {
        return nullptr;
    }

    if (GraphKind.Equals(TEXT("macro"), ESearchCase::IgnoreCase))
    {
        FBlueprintEditorUtils::AddMacroGraph(Blueprint, NewGraph, true, nullptr);
    }
    else
    {
        FBlueprintEditorUtils::AddUbergraphPage(Blueprint, NewGraph);
    }

    return NewGraph;
}
} // namespace

FEpicUnrealMCPBlueprintCommands::FEpicUnrealMCPBlueprintCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_blueprint"))
    {
        return HandleCreateBlueprint(Params);
    }
    else if (CommandType == TEXT("create_blueprint_interface"))
    {
        return HandleCreateBlueprintInterface(Params);
    }
    else if (CommandType == TEXT("add_component_to_blueprint"))
    {
        return HandleAddComponentToBlueprint(Params);
    }
    else if (CommandType == TEXT("set_physics_properties"))
    {
        return HandleSetPhysicsProperties(Params);
    }
    else if (CommandType == TEXT("compile_blueprint"))
    {
        return HandleCompileBlueprint(Params);
    }
    else if (CommandType == TEXT("set_static_mesh_properties"))
    {
        return HandleSetStaticMeshProperties(Params);
    }
    else if (CommandType == TEXT("set_skeletal_mesh_properties"))
    {
        return HandleSetSkeletalMeshProperties(Params);
    }
    else if (CommandType == TEXT("copy_value"))
    {
        return HandleCopyValue(Params);
    }
    else if (CommandType == TEXT("find_and_replace"))
    {
        return HandleFindAndReplace(Params);
    }
    else if (CommandType == TEXT("run_python"))
    {
        return HandleRunPython(Params);
    }
    else if (CommandType == TEXT("execute_unreal_python"))
    {
        return HandleExecuteUnrealPython(Params);
    }
    else if (CommandType == TEXT("exportblueprint") || CommandType == TEXT("export_blueprint"))
    {
        return HandleExportBlueprint(Params);
    }
    else if (CommandType == TEXT("bp_agent"))
    {
        return HandleBpAgent(Params);
    }
    else if (CommandType == TEXT("fetch_blueprint_best_practices"))
    {
        return HandleFetchBlueprintBestPractices(Params);
    }
    else if (CommandType == TEXT("get_asset_meta") || CommandType == TEXT("get_blueprint_meta"))
    {
        return HandleGetBlueprintMeta(Params);
    }
    else if (CommandType == TEXT("export_asset_meta") || CommandType == TEXT("export_blueprint_meta"))
    {
        return HandleExportBlueprintMeta(Params);
    }
    else if (CommandType == TEXT("get_asset_py") || CommandType == TEXT("get_blueprint_py"))
    {
        return HandleGetBlueprintPython(Params);
    }
    else if (CommandType == TEXT("export_asset_py") || CommandType == TEXT("export_blueprint_py"))
    {
        return HandleExportBlueprintPython(Params);
    }
    else if (CommandType == TEXT("get_asset_bpy") || CommandType == TEXT("get_blueprint_bpy"))
    {
        return HandleGetBlueprintBpy(Params);
    }
    else if (CommandType == TEXT("export_asset_bpy") || CommandType == TEXT("export_blueprint_bpy"))
    {
        return HandleExportBlueprintBpy(Params);
    }
    else if (
        CommandType == TEXT("import_asset_py") ||
        CommandType == TEXT("import_blueprint_py") ||
        CommandType == TEXT("import_blueprint_from_bpy") ||
        CommandType == TEXT("edit_blueprint_by_bpy"))
    {
        return HandleImportBlueprintPython(Params);
    }
    else if (CommandType == TEXT("export_blueprint_functions") || CommandType == TEXT("split_blueprint_functions"))
    {
        return HandleExportBlueprintFunctions(Params);
    }
    else if (CommandType == TEXT("import_blueprint_functions") || CommandType == TEXT("reimport_blueprint_functions"))
    {
        return HandleImportBlueprintFunctions(Params);
    }
    else if (CommandType == TEXT("compile_live_coding") || CommandType == TEXT("compile_cpp") || CommandType == TEXT("compile_unreal_code"))
    {
        return HandleCompileLiveCoding(Params);
    }
    else if (CommandType == TEXT("get_live_coding_log"))
    {
        return HandleGetLiveCodingLog(Params);
    }
    else if (CommandType == TEXT("trigger_live_coding"))
    {
        return HandleTriggerLiveCoding(Params);
    }
    else if (CommandType == TEXT("set_gamemode_default_spawn_class") || CommandType == TEXT("set_gamemode_default_pawn_class"))
    {
        return HandleSetGameModeDefaultSpawnClass(Params);
    }
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    else if (CommandType == TEXT("spawn_blueprint_actors"))
    {
        return HandleSpawnBlueprintActors(Params);
    }
    else if (CommandType == TEXT("set_mesh_material_color"))
    {
        return HandleSetMeshMaterialColor(Params);
    }
    // Material management commands
    else if (CommandType == TEXT("get_available_materials"))
    {
        return HandleGetAvailableMaterials(Params);
    }
    else if (CommandType == TEXT("apply_material_to_actor"))
    {
        return HandleApplyMaterialToActor(Params);
    }
    else if (CommandType == TEXT("apply_material_to_blueprint"))
    {
        return HandleApplyMaterialToBlueprint(Params);
    }
    else if (CommandType == TEXT("get_actor_material_info"))
    {
        return HandleGetActorMaterialInfo(Params);
    }
    else if (CommandType == TEXT("get_blueprint_material_info"))
    {
        return HandleGetBlueprintMaterialInfo(Params);
    }
    // Blueprint analysis commands
    else if (CommandType == TEXT("read_blueprint_content"))
    {
        return HandleReadBlueprintContent(Params);
    }
    else if (CommandType == TEXT("read_blueprint_content_fast"))
    {
        return HandleReadBlueprintContentFast(Params);
    }
    else if (CommandType == TEXT("analyze_blueprint_graph"))
    {
        return HandleAnalyzeBlueprintGraph(Params);
    }
    else if (CommandType == TEXT("analyze_blueprint_graph_fast"))
    {
        return HandleAnalyzeBlueprintGraphFast(Params);
    }
    else if (CommandType == TEXT("get_blueprint_properties"))
    {
        return HandleGetBlueprintProperties(Params);
    }
    else if (CommandType == TEXT("get_blueprint_component_properties"))
    {
        return HandleGetBlueprintComponentProperties(Params);
    }
    else if (CommandType == TEXT("get_blueprint_properties_specifiers"))
    {
        return HandleGetBlueprintPropertiesSpecifiers(Params);
    }
    else if (CommandType == TEXT("get_blueprint_variable_details"))
    {
        return HandleGetBlueprintVariableDetails(Params);
    }
    else if (CommandType == TEXT("get_blueprint_function_details"))
    {
        return HandleGetBlueprintFunctionDetails(Params);
    }
    else if (CommandType == TEXT("get_blueprint_class_info"))
    {
        return HandleGetBlueprintClassInfo(Params);
    }
    else if (CommandType == TEXT("edit_blueprint"))
    {
        return HandleEditBlueprint(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Check if blueprint already exists
    FString PackagePath = TEXT("/Game/Blueprints/");
    FString AssetName = BlueprintName;
    if (UEditorAssetLibrary::DoesAssetExist(PackagePath + AssetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint already exists: %s"), *BlueprintName));
    }

    // Create the blueprint factory
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    
    // Handle parent class
    FString ParentClass;
    Params->TryGetStringField(TEXT("parent_class"), ParentClass);
    
    // Default to Actor if no parent class specified
    UClass* SelectedParentClass = AActor::StaticClass();
    
    // Try to find the specified parent class
    if (!ParentClass.IsEmpty())
    {
        FString ClassName = ParentClass;
        if (!ClassName.StartsWith(TEXT("A")))
        {
            ClassName = TEXT("A") + ClassName;
        }
        
        // First try direct StaticClass lookup for common classes
        UClass* FoundClass = nullptr;
        if (ClassName == TEXT("APawn"))
        {
            FoundClass = APawn::StaticClass();
        }
        else if (ClassName == TEXT("AActor"))
        {
            FoundClass = AActor::StaticClass();
        }
        else
        {
            // Try loading the class using LoadClass which is more reliable than FindObject
            const FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
            FoundClass = LoadClass<AActor>(nullptr, *ClassPath);
            
            if (!FoundClass)
            {
                // Try alternate paths if not found
                const FString GameClassPath = FString::Printf(TEXT("/Script/Game.%s"), *ClassName);
                FoundClass = LoadClass<AActor>(nullptr, *GameClassPath);
            }
        }

        if (FoundClass)
        {
            SelectedParentClass = FoundClass;
            UE_LOG(LogTemp, Log, TEXT("Successfully set parent class to '%s'"), *ClassName);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find specified parent class '%s' at paths: /Script/Engine.%s or /Script/Game.%s, defaulting to AActor"), 
                *ClassName, *ClassName, *ClassName);
        }
    }
    
    Factory->ParentClass = SelectedParentClass;

    // Create the blueprint
    UPackage* Package = CreatePackage(*(PackagePath + AssetName));
    UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (NewBlueprint)
    {
        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewBlueprint);

        // Mark the package dirty
        Package->MarkPackageDirty();

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("name"), AssetName);
        ResultObj->SetStringField(TEXT("path"), PackagePath + AssetName);
        return ResultObj;
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create blueprint"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
    FString SoftPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("soft_path_from_project_root"), SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'soft_path_from_project_root' parameter"));
    }

    SoftPath.TrimStartAndEndInline();
    if (SoftPath.IsEmpty() || !SoftPath.StartsWith(TEXT("/Game/")))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint Interface path must be a /Game/... soft path"));
    }

    if (SoftPath.Contains(TEXT(".")))
    {
        SoftPath = FPackageName::ObjectPathToPackageName(SoftPath);
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(SoftPath);
    if (AssetName.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to resolve interface asset name from soft path"));
    }

    if (UEditorAssetLibrary::DoesAssetExist(SoftPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset already exists: %s"), *SoftPath));
    }

    UPackage* Package = CreatePackage(*SoftPath);
    if (!Package)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *SoftPath));
    }

    UBlueprint* NewInterface = FKismetEditorUtilities::CreateBlueprint(
        UInterface::StaticClass(),
        Package,
        FName(*AssetName),
        BPTYPE_Interface,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(),
        FName(TEXT("UnrealMCP")));

    if (!NewInterface)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Blueprint Interface"));
    }

    FAssetRegistryModule::AssetCreated(NewInterface);
    Package->MarkPackageDirty();

    TArray<TSharedPtr<FJsonValue>> InterfaceFunctionSpecs;
    const TArray<TSharedPtr<FJsonValue>>* InterfaceFunctionArray = nullptr;
    if (Params->TryGetArrayField(TEXT("interface_functions"), InterfaceFunctionArray) && InterfaceFunctionArray)
    {
        InterfaceFunctionSpecs = *InterfaceFunctionArray;
    }
    else
    {
        FString InterfaceFunctionsJson;
        if (Params->TryGetStringField(TEXT("interface_functions"), InterfaceFunctionsJson))
        {
            InterfaceFunctionsJson.TrimStartAndEndInline();
            if (InterfaceFunctionsJson.IsEmpty())
            {
                InterfaceFunctionsJson.Empty();
            }
        }

        if (!InterfaceFunctionsJson.IsEmpty())
        {
            if (!TryDeserializeJsonArray_BP(InterfaceFunctionsJson, InterfaceFunctionSpecs))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to parse 'interface_functions' JSON array"));
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> FunctionResults;
    TArray<TSharedPtr<FJsonValue>> Errors;

    auto AddFunctionError = [&Errors](const FString& Message)
    {
        Errors.Add(MakeShared<FJsonValueString>(Message));
    };

    for (int32 FunctionIndex = 0; FunctionIndex < InterfaceFunctionSpecs.Num(); ++FunctionIndex)
    {
        const TSharedPtr<FJsonValue>& FunctionValue = InterfaceFunctionSpecs[FunctionIndex];
        if (!FunctionValue.IsValid() || FunctionValue->Type != EJson::Object)
        {
            AddFunctionError(FString::Printf(TEXT("interface_functions[%d] must be an object"), FunctionIndex));
            continue;
        }

        const TSharedPtr<FJsonObject> FunctionSpec = FunctionValue->AsObject();
        FString FunctionName;
        if (!FunctionSpec->TryGetStringField(TEXT("name"), FunctionName))
        {
            AddFunctionError(FString::Printf(TEXT("interface_functions[%d] is missing 'name'"), FunctionIndex));
            continue;
        }
        FunctionName.TrimStartAndEndInline();
        if (FunctionName.IsEmpty())
        {
            AddFunctionError(FString::Printf(TEXT("interface_functions[%d] is missing 'name'"), FunctionIndex));
            continue;
        }

        TSharedPtr<FJsonObject> CreateFunctionParams = MakeShared<FJsonObject>();
        CreateFunctionParams->SetStringField(TEXT("blueprint_name"), NewInterface->GetPathName());
        CreateFunctionParams->SetStringField(TEXT("function_name"), FunctionName);
        TSharedPtr<FJsonObject> CreateFunctionResult = FFunctionManager::CreateFunction(CreateFunctionParams);
        if (!CreateFunctionResult.IsValid() || !CreateFunctionResult->GetBoolField(TEXT("success")))
        {
            AddFunctionError(FString::Printf(TEXT("Failed to create interface function '%s'"), *FunctionName));
            continue;
        }

        FunctionResults.Add(MakeShared<FJsonValueObject>(CreateFunctionResult));

        auto AddParamsFromObject = [&](const TCHAR* FieldName, const FString& Direction)
        {
            const TSharedPtr<FJsonObject>* ParamObject = nullptr;
            if (!FunctionSpec->TryGetObjectField(FieldName, ParamObject) || !ParamObject || !ParamObject->IsValid())
            {
                return;
            }

            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ParamObject)->Values)
            {
                FString ParamType;
                if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(ParamType))
                {
                    AddFunctionError(FString::Printf(TEXT("Function '%s' parameter '%s' must map to a string type"), *FunctionName, *Pair.Key));
                    continue;
                }

                bool bIsArray = false;
                ParamType.TrimStartAndEndInline();
                if (ParamType.StartsWith(TEXT("array:"), ESearchCase::IgnoreCase))
                {
                    bIsArray = true;
                    ParamType = ParamType.Mid(6);
                }

                TSharedPtr<FJsonObject> ParamParams = MakeShared<FJsonObject>();
                ParamParams->SetStringField(TEXT("blueprint_name"), NewInterface->GetPathName());
                ParamParams->SetStringField(TEXT("function_name"), FunctionName);
                ParamParams->SetStringField(TEXT("param_name"), Pair.Key);
                ParamParams->SetStringField(TEXT("param_type"), ParamType);
                ParamParams->SetStringField(TEXT("direction"), Direction);
                ParamParams->SetBoolField(TEXT("is_array"), bIsArray);

                TSharedPtr<FJsonObject> ParamResult = FFunctionIO::AddFunctionIO(ParamParams);
                if (!ParamResult.IsValid() || !ParamResult->GetBoolField(TEXT("success")))
                {
                    AddFunctionError(FString::Printf(TEXT("Failed to add %s parameter '%s' on interface function '%s'"), *Direction, *Pair.Key, *FunctionName));
                    continue;
                }

                FunctionResults.Add(MakeShared<FJsonValueObject>(ParamResult));
            }
        };

        AddParamsFromObject(TEXT("inputs"), TEXT("input"));
        AddParamsFromObject(TEXT("outputs"), TEXT("output"));
    }

    TSharedPtr<FJsonObject> CompileParams = MakeShared<FJsonObject>();
    CompileParams->SetStringField(TEXT("blueprint_name"), NewInterface->GetPathName());
    TSharedPtr<FJsonObject> CompileResult = HandleCompileBlueprint(CompileParams);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0 && CompileResult.IsValid() && CompileResult->GetBoolField(TEXT("success")));
    ResultObj->SetStringField(TEXT("asset_path"), NewInterface->GetPathName());
    ResultObj->SetStringField(TEXT("interface_name"), AssetName);
    ResultObj->SetArrayField(TEXT("function_results"), FunctionResults);
    ResultObj->SetNumberField(TEXT("function_result_count"), FunctionResults.Num());
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());
    ResultObj->SetObjectField(TEXT("compile_result"), CompileResult);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentType;
    if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (FindBlueprintComponentNodeByName_BP(Blueprint, ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component already exists: %s"), *ComponentName));
    }

    UClass* ComponentClass = ResolveClassByReference(ComponentType);
    if (!ComponentClass)
    {
        ComponentClass = FindObject<UClass>(nullptr, *ComponentType);
    }

    if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
    {
        FString ComponentTypeWithSuffix = ComponentType + TEXT("Component");
        ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithSuffix);
    }

    if (!ComponentClass && !ComponentType.StartsWith(TEXT("U")))
    {
        FString ComponentTypeWithPrefix = TEXT("U") + ComponentType;
        ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithPrefix);
        if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
        {
            FString ComponentTypeWithBoth = TEXT("U") + ComponentType + TEXT("Component");
            ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithBoth);
        }
    }

    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown component type: %s"), *ComponentType));
    }

    USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
    if (!NewNode || !NewNode->ComponentTemplate)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create component node"));
    }

    TArray<TSharedPtr<FJsonValue>> AppliedFields;
    if (USceneComponent* SceneComponent = Cast<USceneComponent>(NewNode->ComponentTemplate))
    {
        ApplySceneComponentOverrides_BP(SceneComponent, Params, AppliedFields);
    }

    const TSharedPtr<FJsonObject>* ComponentProperties = nullptr;
    TArray<TSharedPtr<FJsonValue>> AppliedPropertyArray;
    FString PropertyError;
    if (Params->TryGetObjectField(TEXT("component_properties"), ComponentProperties) && ComponentProperties && ComponentProperties->IsValid())
    {
        if (!ApplyPropertyMapToObject_BP(NewNode->ComponentTemplate, *ComponentProperties, AppliedPropertyArray, PropertyError))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to apply component_properties for '%s': %s"), *ComponentName, *PropertyError));
        }
    }

    FString ParentComponentName;
    Params->TryGetStringField(TEXT("parent_component_name"), ParentComponentName);
    if (ParentComponentName.IsEmpty())
    {
        Params->TryGetStringField(TEXT("parent_name"), ParentComponentName);
    }

    FString AttachToName;
    Params->TryGetStringField(TEXT("attach_to_name"), AttachToName);
    if (!AttachToName.IsEmpty())
    {
        NewNode->AttachToName = FName(*AttachToName);
    }

    if (!ParentComponentName.IsEmpty())
    {
        USCS_Node* ParentNode = FindBlueprintComponentNodeByName_BP(Blueprint, ParentComponentName);
        if (ParentNode)
        {
            ParentNode->AddChildNode(NewNode);
        }
        else if (USceneComponent* ParentSceneComponent = Cast<USceneComponent>(ResolveBlueprintEndpointObject(Blueprint, ParentComponentName)))
        {
            NewNode->SetParent(ParentSceneComponent);
            Blueprint->SimpleConstructionScript->AddNode(NewNode);
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Parent component not found: %s"), *ParentComponentName));
        }
    }
    else
    {
        Blueprint->SimpleConstructionScript->AddNode(NewNode);
    }

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    if (bCompileBlueprint)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetStringField(TEXT("component_type"), ComponentClass->GetName());
    ResultObj->SetStringField(TEXT("component_class_path"), ComponentClass->GetPathName());
    ResultObj->SetStringField(TEXT("component_template_path"), NewNode->ComponentTemplate->GetPathName());
    ResultObj->SetBoolField(TEXT("compiled"), bCompileBlueprint);
    ResultObj->SetStringField(TEXT("parent_component_name"), ParentComponentName);
    ResultObj->SetStringField(TEXT("attach_to_name"), AttachToName);
    ResultObj->SetArrayField(TEXT("transform_fields_applied"), AppliedFields);
    ResultObj->SetArrayField(TEXT("component_properties_applied"), AppliedPropertyArray);
    ResultObj->SetNumberField(TEXT("component_properties_applied_count"), AppliedPropertyArray.Num());
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Set physics properties
    if (Params->HasField(TEXT("simulate_physics")))
    {
        PrimComponent->SetSimulatePhysics(Params->GetBoolField(TEXT("simulate_physics")));
    }

    if (Params->HasField(TEXT("mass")))
    {
        float Mass = Params->GetNumberField(TEXT("mass"));
        // In UE5.5, use proper overrideMass instead of just scaling
        PrimComponent->SetMassOverrideInKg(NAME_None, Mass);
        UE_LOG(LogTemp, Display, TEXT("Set mass for component %s to %f kg"), *ComponentName, Mass);
    }

    if (Params->HasField(TEXT("linear_damping")))
    {
        PrimComponent->SetLinearDamping(Params->GetNumberField(TEXT("linear_damping")));
    }

    if (Params->HasField(TEXT("angular_damping")))
    {
        PrimComponent->SetAngularDamping(Params->GetNumberField(TEXT("angular_damping")));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        Params->TryGetStringField(TEXT("blueprint_path"), BlueprintName);
    }
    if (BlueprintName.IsEmpty())
    {
        Params->TryGetStringField(TEXT("soft_path_from_project_root"), BlueprintName);
    }
    if (BlueprintName.IsEmpty())
    {
        Params->TryGetStringField(TEXT("target"), BlueprintName);
    }
    if (BlueprintName.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Compile the blueprint with results log for detailed messages
    FCompilerResultsLog ResultsLog;
    ResultsLog.bAnnotateMentionedNodes = false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &ResultsLog);

    const bool bHasErrors = ResultsLog.NumErrors > 0;
    const bool bCompiledOk = !bHasErrors && Blueprint->Status != BS_Error;

    TArray<TSharedPtr<FJsonValue>> ErrorArray;
    TArray<TSharedPtr<FJsonValue>> WarningArray;
    TArray<TSharedPtr<FJsonValue>> InfoArray;

    for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
    {
        const FString MsgText = Msg->ToText().ToString();
        switch (Msg->GetSeverity())
        {
        case EMessageSeverity::Error:
            ErrorArray.Add(MakeShared<FJsonValueString>(MsgText));
            break;
        case EMessageSeverity::Warning:
        case EMessageSeverity::PerformanceWarning:
            WarningArray.Add(MakeShared<FJsonValueString>(MsgText));
            break;
        default:
            InfoArray.Add(MakeShared<FJsonValueString>(MsgText));
            break;
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), bCompiledOk);
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetBoolField(TEXT("compiled"), bCompiledOk);
    ResultObj->SetNumberField(TEXT("num_errors"), ResultsLog.NumErrors);
    ResultObj->SetNumberField(TEXT("num_warnings"), ResultsLog.NumWarnings);
    ResultObj->SetArrayField(TEXT("errors"), ErrorArray);
    ResultObj->SetArrayField(TEXT("warnings"), WarningArray);
    ResultObj->SetArrayField(TEXT("infos"), InfoArray);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSpawnBlueprintActors(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* Actors = nullptr;
    if (!Params || !Params->TryGetArrayField(TEXT("actors"), Actors))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actors' parameter"));
    }

    TArray<TSharedPtr<FJsonValue>> SpawnedActors;
    TArray<TSharedPtr<FJsonValue>> Errors;

    for (int32 Index = 0; Index < Actors->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& ActorValue = (*Actors)[Index];
        if (!ActorValue.IsValid() || ActorValue->Type != EJson::Object)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("actors[%d]: entry must be an object"), Index)));
            continue;
        }

        TSharedPtr<FJsonObject> SpawnParams = MakeShared<FJsonObject>(*ActorValue->AsObject());

        FString BlueprintPath;
        if (!SpawnParams->TryGetStringField(TEXT("blueprint_name"), BlueprintPath))
        {
            SpawnParams->TryGetStringField(TEXT("soft_path_from_project_root"), BlueprintPath);
        }

        if (BlueprintPath.IsEmpty())
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("actors[%d]: missing soft_path_from_project_root"), Index)));
            continue;
        }

        SpawnParams->SetStringField(TEXT("blueprint_name"), BlueprintPath);

        TSharedPtr<FJsonObject> SpawnResult = HandleSpawnBlueprintActor(SpawnParams);
        if (!SpawnResult.IsValid() || (SpawnResult->HasField(TEXT("success")) && !SpawnResult->GetBoolField(TEXT("success"))))
        {
            const FString ErrorMessage = SpawnResult.IsValid() && SpawnResult->HasField(TEXT("error"))
                ? SpawnResult->GetStringField(TEXT("error"))
                : TEXT("spawn failed");
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("actors[%d]: %s"), Index, *ErrorMessage)));
            continue;
        }

        SpawnedActors.Add(MakeShared<FJsonValueObject>(SpawnResult));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), Errors.Num() == 0);
    ResultObj->SetArrayField(TEXT("actors"), SpawnedActors);
    ResultObj->SetArrayField(TEXT("errors"), Errors);
    ResultObj->SetNumberField(TEXT("spawned_count"), SpawnedActors.Num());
    ResultObj->SetNumberField(TEXT("error_count"), Errors.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Starting blueprint actor spawn"));
    
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Missing blueprint_name parameter"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Missing actor_name parameter"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Looking for blueprint '%s'"), *BlueprintName);

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Blueprint not found: %s"), *BlueprintName);
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Blueprint found, getting transform parameters"));

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Location set to (%f, %f, %f)"), Location.X, Location.Y, Location.Z);
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Rotation set to (%f, %f, %f)"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Getting editor world"));

    // Spawn the actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Failed to get editor world"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Creating spawn transform"));

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));

    // Add a small delay to allow the engine to process the newly compiled class
    FPlatformProcess::Sleep(0.2f);

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintGeneratedClassSafe(Blueprint);
    if (!BlueprintClass)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint '%s' has no compiled GeneratedClass"), *BlueprintName));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: About to spawn actor from blueprint '%s' with GeneratedClass: %s"), 
           *BlueprintName, *BlueprintClass->GetName());

    AActor* NewActor = World->SpawnActor<AActor>(BlueprintClass, SpawnTransform);
    
    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: SpawnActor completed, NewActor: %s"), 
           NewActor ? *NewActor->GetName() : TEXT("NULL"));
    
    if (NewActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Setting actor label to '%s'"), *ActorName);
        NewActor->SetActorLabel(*ActorName);
        
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: About to convert actor to JSON"));
        TSharedPtr<FJsonObject> Result = FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
        
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: JSON conversion completed, returning result"));
        return Result;
    }

    UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Failed to spawn blueprint actor"));
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Set static mesh properties
    if (Params->HasField(TEXT("static_mesh")))
    {
        FString MeshPath = Params->GetStringField(TEXT("static_mesh"));
        UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
        if (Mesh)
        {
            MeshComponent->SetStaticMesh(Mesh);
        }
    }

    if (Params->HasField(TEXT("material")))
    {
        FString MaterialPath = Params->GetStringField(TEXT("material"));
        UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (Material)
        {
            MeshComponent->SetMaterial(0, Material);
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetSkeletalMeshProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    USkeletalMeshComponent* MeshComponent = nullptr;

    // Try to find the component in the SCS (non-inherited components)
    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString() == ComponentName)
            {
                MeshComponent = Cast<USkeletalMeshComponent>(Node->ComponentTemplate);
                break;
            }
        }
    }

    // Fallback: try to find inherited components on the CDO (e.g., Character's Mesh)
    if (!MeshComponent)
    {
        if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(FEpicUnrealMCPCommonUtils::GetBlueprintGeneratedClassSafe(Blueprint)))
        {
            UObject* CDO = BPGC->GetDefaultObject();
            if (ACharacter* CharacterCDO = Cast<ACharacter>(CDO))
            {
                // ACharacter has a built-in Mesh component
                MeshComponent = CharacterCDO->GetMesh();
            }

            if (!MeshComponent)
            {
                if (AActor* ActorCDO = Cast<AActor>(CDO))
                {
                    TArray<UActorComponent*> Components;
                    ActorCDO->GetComponents(Components);
                    for (UActorComponent* Comp : Components)
                    {
                        if (Comp && Comp->GetName() == ComponentName)
                        {
                            MeshComponent = Cast<USkeletalMeshComponent>(Comp);
                            if (MeshComponent)
                            {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!MeshComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component not found or not a skeletal mesh component"));
    }

    // Set skeletal mesh
    if (Params->HasField(TEXT("skeletal_mesh")))
    {
        FString MeshPath = Params->GetStringField(TEXT("skeletal_mesh"));
        USkeletalMesh* Mesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
        if (!Mesh)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load skeletal mesh: %s"), *MeshPath));
        }
        MeshComponent->SetSkeletalMesh(Mesh);
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCopyValue(const TSharedPtr<FJsonObject>& Params)
{
    const TSharedPtr<FJsonObject>* SourceJson = nullptr;
    if (!Params->TryGetObjectField(TEXT("source"), SourceJson) || !SourceJson || !SourceJson->IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source' object"));
    }

    const TSharedPtr<FJsonObject>* TargetJson = nullptr;
    if (!Params->TryGetObjectField(TEXT("target"), TargetJson) || !TargetJson || !TargetJson->IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target' object"));
    }

    FCopyValueEndpoint SourceEndpoint;
    FCopyValueEndpoint TargetEndpoint;
    FString ParseError;
    if (!ParseCopyValueEndpoint(*SourceJson, TEXT("source"), SourceEndpoint, ParseError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
    }
    if (!ParseCopyValueEndpoint(*TargetJson, TEXT("target"), TargetEndpoint, ParseError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
    }

    UBlueprint* SourceBlueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(SourceEndpoint.BlueprintName);
    if (!SourceBlueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Source blueprint not found: %s"), *SourceEndpoint.BlueprintName));
    }

    UBlueprint* TargetBlueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(TargetEndpoint.BlueprintName);
    if (!TargetBlueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Target blueprint not found: %s"), *TargetEndpoint.BlueprintName));
    }

    UObject* SourceObject = ResolveBlueprintEndpointObject(SourceBlueprint, SourceEndpoint.ComponentName);
    if (!SourceObject)
    {
        const FString Scope = SourceEndpoint.ComponentName.IsEmpty() ? TEXT("CDO") : SourceEndpoint.ComponentName;
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Source object not found: %s"), *Scope));
    }

    UObject* TargetObject = ResolveBlueprintEndpointObject(TargetBlueprint, TargetEndpoint.ComponentName);
    if (!TargetObject)
    {
        const FString Scope = TargetEndpoint.ComponentName.IsEmpty() ? TEXT("CDO") : TargetEndpoint.ComponentName;
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Target object not found: %s"), *Scope));
    }

    FProperty* SourceProperty = FindPropertyByNameLoose(SourceObject->GetClass(), SourceEndpoint.PropertyName);
    if (!SourceProperty)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Source property not found: %s"), *SourceEndpoint.PropertyName));
    }

    FProperty* TargetProperty = FindPropertyByNameLoose(TargetObject->GetClass(), TargetEndpoint.PropertyName);
    if (!TargetProperty)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Target property not found: %s"), *TargetEndpoint.PropertyName));
    }

    const void* SourceValuePtr = SourceProperty->ContainerPtrToValuePtr<void>(SourceObject);
    void* TargetValuePtr = TargetProperty->ContainerPtrToValuePtr<void>(TargetObject);
    if (!SourceValuePtr || !TargetValuePtr)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to resolve property value pointers"));
    }

    TargetObject->Modify();

    FString CopyError;
    if (!TryCopyPropertyValue(SourceProperty, SourceValuePtr, TargetProperty, TargetValuePtr, CopyError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Copy failed (%s.%s -> %s.%s): %s"),
                *SourceEndpoint.ComponentName,
                *SourceEndpoint.PropertyName,
                *TargetEndpoint.ComponentName,
                *TargetEndpoint.PropertyName,
                *CopyError));
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(TargetBlueprint);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);
    if (bCompileBlueprint)
    {
        FKismetEditorUtilities::CompileBlueprint(TargetBlueprint);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("source_blueprint"), SourceBlueprint->GetPathName());
    ResultObj->SetStringField(TEXT("target_blueprint"), TargetBlueprint->GetPathName());
    ResultObj->SetStringField(TEXT("source_object"), SourceObject->GetPathName());
    ResultObj->SetStringField(TEXT("target_object"), TargetObject->GetPathName());
    ResultObj->SetStringField(TEXT("source_property"), SourceProperty->GetName());
    ResultObj->SetStringField(TEXT("target_property"), TargetProperty->GetName());
    ResultObj->SetBoolField(TEXT("compiled"), bCompileBlueprint);
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleFindAndReplace(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("source"), BlueprintName))
    {
        Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName);
    }
    if (BlueprintName.IsEmpty())
    {
        Params->TryGetStringField(TEXT("blueprint_path"), BlueprintName);
    }
    if (BlueprintName.IsEmpty())
    {
        Params->TryGetStringField(TEXT("blueprint"), BlueprintName);
    }

    const TSharedPtr<FJsonObject>* SourceObj = nullptr;
    if (BlueprintName.IsEmpty() && Params->TryGetObjectField(TEXT("source"), SourceObj) && SourceObj && SourceObj->IsValid())
    {
        (*SourceObj)->TryGetStringField(TEXT("blueprint_name"), BlueprintName);
        if (BlueprintName.IsEmpty())
        {
            (*SourceObj)->TryGetStringField(TEXT("blueprint_path"), BlueprintName);
        }
        if (BlueprintName.IsEmpty())
        {
            (*SourceObj)->TryGetStringField(TEXT("blueprint"), BlueprintName);
        }
    }

    if (BlueprintName.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing source blueprint (source/blueprint_name/blueprint_path)"));
    }

    FString OldRef;
    if (!Params->TryGetStringField(TEXT("old_ref"), OldRef))
    {
        Params->TryGetStringField(TEXT("oldRef"), OldRef);
    }

    FString NewRef;
    if (!Params->TryGetStringField(TEXT("new_ref"), NewRef))
    {
        Params->TryGetStringField(TEXT("newRef"), NewRef);
    }

    if (OldRef.IsEmpty() || NewRef.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing old_ref/new_ref parameters"));
    }

    const FString OldRefNormalized = NormalizeReferenceString(OldRef);
    const FString NewRefNormalized = NormalizeReferenceString(NewRef);

    UObject* NewRefObject = ResolveObjectByReference(NewRef);
    if (!NewRefObject && !NewRefNormalized.IsEmpty())
    {
        NewRefObject = ResolveObjectByReference(NewRefNormalized);
    }
    if (!NewRefObject)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to load new_ref object: %s"), *NewRef));
    }

    UClass* NewRefClass = ResolveClassByReference(NewRef);
    if (!NewRefClass && !NewRefNormalized.IsEmpty())
    {
        NewRefClass = ResolveClassByReference(NewRefNormalized);
    }

    UClass* OldRefClass = ResolveClassByReference(OldRef);
    if (!OldRefClass && !OldRefNormalized.IsEmpty())
    {
        OldRefClass = ResolveClassByReference(OldRefNormalized);
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    FString GraphFilter;
    Params->TryGetStringField(TEXT("graph_name"), GraphFilter);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    bool bModifiedBlueprint = false;
    int32 UpdatedVariableReferences = 0;
    int32 UpdatedDynamicCastTargets = 0;
    int32 UpdatedPinTypeReferences = 0;
    int32 UpdatedNodeObjectProperties = 0;
    int32 ScannedNodes = 0;

    TArray<TSharedPtr<FJsonValue>> ChangedNodeArray;

    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph)
        {
            continue;
        }

        if (!GraphFilter.IsEmpty() && !Graph->GetName().Equals(GraphFilter, ESearchCase::IgnoreCase))
        {
            continue;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            ++ScannedNodes;
            bool bNodeChanged = false;

            if (UK2Node_DynamicCast* DynamicCastNode = Cast<UK2Node_DynamicCast>(Node))
            {
                if (DynamicCastNode->TargetType)
                {
                    const FString TargetPath = DynamicCastNode->TargetType->GetPathName();
                    const bool bTargetPathMatch = DoesReferenceMatch(TargetPath, OldRef, OldRefNormalized);
                    const bool bTargetClassMatch = OldRefClass && DynamicCastNode->TargetType == OldRefClass;
                    if ((bTargetPathMatch || bTargetClassMatch) && NewRefClass)
                    {
                        DynamicCastNode->Modify();
                        DynamicCastNode->TargetType = NewRefClass;
                        DynamicCastNode->ReconstructNode();
                        ++UpdatedDynamicCastTargets;
                        bNodeChanged = true;
                    }
                }
            }

            if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
            {
                UClass* ScopeClass = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass : Blueprint->GeneratedClass;
                UClass* ParentClass = VariableNode->VariableReference.GetMemberParentClass(ScopeClass);
                const bool bParentPathMatch = ParentClass && DoesReferenceMatch(ParentClass->GetPathName(), OldRef, OldRefNormalized);
                const bool bParentClassMatch = ParentClass && OldRefClass && ParentClass == OldRefClass;

                if ((bParentPathMatch || bParentClassMatch) && NewRefClass)
                {
                    const FName MemberName = VariableNode->VariableReference.GetMemberName();
                    VariableNode->Modify();
                    VariableNode->VariableReference.SetExternalMember(MemberName, NewRefClass);
                    VariableNode->ReconstructNode();
                    ++UpdatedVariableReferences;
                    bNodeChanged = true;
                }
            }

            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin)
                {
                    continue;
                }

                UObject* PinTypeObject = Pin->PinType.PinSubCategoryObject.Get();
                if (!PinTypeObject)
                {
                    continue;
                }

                const bool bPinPathMatch = DoesReferenceMatch(PinTypeObject->GetPathName(), OldRef, OldRefNormalized);
                const bool bPinClassMatch = OldRefClass && PinTypeObject == OldRefClass;
                if (!bPinPathMatch && !bPinClassMatch)
                {
                    continue;
                }

                Node->Modify();
                Pin->Modify();
                Pin->PinType.PinSubCategoryObject = NewRefObject;
                ++UpdatedPinTypeReferences;
                bNodeChanged = true;
            }

            for (TFieldIterator<FObjectPropertyBase> It(Node->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
            {
                FObjectPropertyBase* ObjectProperty = *It;
                if (!ObjectProperty)
                {
                    continue;
                }

                UObject* PropertyValue = ObjectProperty->GetObjectPropertyValue_InContainer(Node);
                if (!PropertyValue)
                {
                    continue;
                }

                const bool bPropertyPathMatch = DoesReferenceMatch(PropertyValue->GetPathName(), OldRef, OldRefNormalized);
                const bool bPropertyClassMatch = OldRefClass && PropertyValue == OldRefClass;
                if (!bPropertyPathMatch && !bPropertyClassMatch)
                {
                    continue;
                }

                UObject* ReplacementValue = nullptr;
                if (ObjectProperty->PropertyClass && NewRefObject->IsA(ObjectProperty->PropertyClass))
                {
                    ReplacementValue = NewRefObject;
                }
                else if (NewRefClass && ObjectProperty->PropertyClass &&
                    (NewRefClass == ObjectProperty->PropertyClass || NewRefClass->IsChildOf(ObjectProperty->PropertyClass)))
                {
                    ReplacementValue = NewRefClass;
                }

                if (!ReplacementValue)
                {
                    continue;
                }

                Node->Modify();
                ObjectProperty->SetObjectPropertyValue_InContainer(Node, ReplacementValue);
                ++UpdatedNodeObjectProperties;
                bNodeChanged = true;
            }

            if (bNodeChanged)
            {
                bModifiedBlueprint = true;

                TSharedPtr<FJsonObject> ChangedNode = MakeShared<FJsonObject>();
                ChangedNode->SetStringField(TEXT("graph"), Graph->GetName());
                ChangedNode->SetStringField(TEXT("node"), Node->GetName());
                ChangedNode->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
                ChangedNodeArray.Add(MakeShared<FJsonValueObject>(ChangedNode));
            }
        }
    }

    if (!bModifiedBlueprint)
    {
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
        ResultObj->SetStringField(TEXT("message"), TEXT("No matching references found"));
        ResultObj->SetNumberField(TEXT("scanned_nodes"), ScannedNodes);
        ResultObj->SetArrayField(TEXT("changed_nodes"), ChangedNodeArray);
        return ResultObj;
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);

    if (bCompileBlueprint)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("old_ref"), OldRef);
    ResultObj->SetStringField(TEXT("new_ref"), NewRef);
    ResultObj->SetBoolField(TEXT("compiled"), bCompileBlueprint);
    ResultObj->SetNumberField(TEXT("scanned_nodes"), ScannedNodes);
    ResultObj->SetNumberField(TEXT("updated_variable_references"), UpdatedVariableReferences);
    ResultObj->SetNumberField(TEXT("updated_dynamic_cast_targets"), UpdatedDynamicCastTargets);
    ResultObj->SetNumberField(TEXT("updated_pin_type_references"), UpdatedPinTypeReferences);
    ResultObj->SetNumberField(TEXT("updated_node_object_properties"), UpdatedNodeObjectProperties);
    ResultObj->SetArrayField(TEXT("changed_nodes"), ChangedNodeArray);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleExecuteUnrealPython(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing parameters"));
    }

    FString ScriptInput = GetFirstNonEmptyStringField_BP(
        Params,
        {
            TEXT("python_script_or_patch"),
            TEXT("script_path"),
            TEXT("python_script_path"),
            TEXT("code")
        });

    if (ScriptInput.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("Missing python script input. Provide 'python_script_or_patch', 'script_path', 'python_script_path', or 'code'."));
    }

    double TimeoutSeconds = 60.0;
    if (Params->HasField(TEXT("timeout_seconds")))
    {
        Params->TryGetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
    }
    TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 1.0, 3600.0);

    const FString ShortDescription = GetFirstNonEmptyStringField_BP(Params, {TEXT("short_description")});
    const FString RequestText = GetFirstNonEmptyStringField_BP(Params, {TEXT("request_text")});

    const FString ScriptId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString CacheRoot = FPaths::Combine(BuildPythonExecutionCacheRoot_BP(), ScriptId);
    IFileManager::Get().MakeDirectory(*CacheRoot, true);

    FString PayloadPath;
    bool bUsingExistingScriptPath = false;
    if (const FString ExistingScriptPath = ResolveExistingPythonScriptPath_BP(ScriptInput); !ExistingScriptPath.IsEmpty())
    {
        PayloadPath = ExistingScriptPath;
        bUsingExistingScriptPath = true;
    }
    else
    {
        PayloadPath = FPaths::Combine(CacheRoot, TEXT("payload.py"));
        if (!FFileHelper::SaveStringToFile(ScriptInput, *PayloadPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to write Python payload file: %s"), *PayloadPath));
        }
    }

    const FString ResultPath = FPaths::Combine(CacheRoot, TEXT("result.json"));
    const FString WrapperPath = FPaths::Combine(CacheRoot, TEXT("wrapper.py"));
    const FString NormalizedPayloadPath = NormalizePythonPathForScriptLiteral_BP(FPaths::ConvertRelativePathToFull(PayloadPath));
    const FString NormalizedResultPath = NormalizePythonPathForScriptLiteral_BP(FPaths::ConvertRelativePathToFull(ResultPath));

    const FString WrapperScript = FString::Printf(
        TEXT("import json\n")
        TEXT("import traceback\n")
        TEXT("payload_path = r'''%s'''\n")
        TEXT("result_path = r'''%s'''\n")
        TEXT("result = {'success': False}\n")
        TEXT("try:\n")
        TEXT("    scope = {'__name__': '__main__', '__file__': payload_path}\n")
        TEXT("    with open(payload_path, 'r', encoding='utf-8') as handle:\n")
        TEXT("        source = handle.read()\n")
        TEXT("    exec(compile(source, payload_path, 'exec'), scope, scope)\n")
        TEXT("    result['success'] = True\n")
        TEXT("except Exception:\n")
        TEXT("    result['error'] = traceback.format_exc()\n")
        TEXT("finally:\n")
        TEXT("    try:\n")
        TEXT("        with open(result_path, 'w', encoding='utf-8') as handle:\n")
        TEXT("            json.dump(result, handle, ensure_ascii=False, indent=2)\n")
        TEXT("    except Exception:\n")
        TEXT("        pass\n"),
        *NormalizedPayloadPath,
        *NormalizedResultPath);

    if (!FFileHelper::SaveStringToFile(WrapperScript, *WrapperPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to write Python wrapper file: %s"), *WrapperPath));
    }

    const FString ExecutablePath = ResolveIsolatedPythonExecutable_BP();
    if (ExecutablePath.IsEmpty() || !FPaths::FileExists(ExecutablePath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to resolve Unreal editor executable for isolated Python execution"));
    }

    const FString ProjectFilePath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    if (ProjectFilePath.IsEmpty() || !FPaths::FileExists(ProjectFilePath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to resolve current project file path for isolated Python execution"));
    }

    const FString NormalizedWrapperPath = NormalizePythonPathForScriptLiteral_BP(FPaths::ConvertRelativePathToFull(WrapperPath));
    const FString CommandLine = FString::Printf(
        TEXT("\"%s\" -ExecutePythonScript=\"%s\" -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput"),
        *ProjectFilePath,
        *NormalizedWrapperPath);

    void* PipeRead = nullptr;
    void* PipeWrite = nullptr;
    FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

    uint32 ProcessId = 0;
    FProcHandle ProcHandle = FPlatformProcess::CreateProc(
        *ExecutablePath,
        *CommandLine,
        true,
        true,
        true,
        &ProcessId,
        0,
        *FPaths::ProjectDir(),
        PipeWrite);

    if (!ProcHandle.IsValid())
    {
        if (PipeRead || PipeWrite)
        {
            FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
        }

        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to launch isolated Unreal Python process: %s %s"), *ExecutablePath, *CommandLine));
    }

    FString ProcessOutput;
    const double StartTimeSeconds = FPlatformTime::Seconds();
    bool bTimedOut = false;
    while (FPlatformProcess::IsProcRunning(ProcHandle))
    {
        if (PipeRead)
        {
            ProcessOutput += FPlatformProcess::ReadPipe(PipeRead);
        }

        if ((FPlatformTime::Seconds() - StartTimeSeconds) >= TimeoutSeconds)
        {
            bTimedOut = true;
            FPlatformProcess::TerminateProc(ProcHandle, true);
            break;
        }

        FPlatformProcess::Sleep(0.1f);
    }

    if (PipeRead)
    {
        ProcessOutput += FPlatformProcess::ReadPipe(PipeRead);
    }

    int32 ExitCode = 0;
    FPlatformProcess::GetProcReturnCode(ProcHandle, &ExitCode);
    FPlatformProcess::CloseProc(ProcHandle);
    if (PipeRead || PipeWrite)
    {
        FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("execution_backend"), TEXT("isolated_editor_process"));
    ResultObj->SetStringField(TEXT("script_id"), ScriptId);
    ResultObj->SetStringField(TEXT("script_path"), FPaths::ConvertRelativePathToFull(PayloadPath));
    ResultObj->SetStringField(TEXT("wrapper_path"), FPaths::ConvertRelativePathToFull(WrapperPath));
    ResultObj->SetStringField(TEXT("result_path"), FPaths::ConvertRelativePathToFull(ResultPath));
    ResultObj->SetStringField(TEXT("project_file"), ProjectFilePath);
    ResultObj->SetStringField(TEXT("executable_path"), ExecutablePath);
    ResultObj->SetStringField(TEXT("command_line"), CommandLine);
    ResultObj->SetStringField(TEXT("process_output"), ProcessOutput);
    ResultObj->SetBoolField(TEXT("used_existing_script_path"), bUsingExistingScriptPath);
    ResultObj->SetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
    ResultObj->SetNumberField(TEXT("process_id"), ProcessId);
    ResultObj->SetNumberField(TEXT("exit_code"), ExitCode);
    ResultObj->SetBoolField(TEXT("timed_out"), bTimedOut);
    ResultObj->SetBoolField(TEXT("executed"), !bTimedOut);

    if (!ShortDescription.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("short_description"), ShortDescription);
    }
    if (!RequestText.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("request_text"), RequestText);
    }

    const TArray<TSharedPtr<FJsonValue>> ProcessLogOutput = BuildProcessLogOutputArray_BP(ProcessOutput);
    if (ProcessLogOutput.Num() > 0)
    {
        ResultObj->SetArrayField(TEXT("log_output"), ProcessLogOutput);
    }

    if (bTimedOut)
    {
        ResultObj->SetBoolField(TEXT("success"), false);
        ResultObj->SetStringField(
            TEXT("error"),
            FString::Printf(TEXT("Isolated Unreal Python execution timed out after %.1f seconds and the worker process was terminated"), TimeoutSeconds));
        return ResultObj;
    }

    FString ResultJsonText;
    if (FFileHelper::LoadFileToString(ResultJsonText, *ResultPath))
    {
        TSharedPtr<FJsonObject> WrapperResult;
        if (TryDeserializeJsonObject_BP(ResultJsonText, WrapperResult) && WrapperResult.IsValid())
        {
            ResultObj->SetObjectField(TEXT("wrapper_result"), WrapperResult);

            bool bWrapperSuccess = false;
            WrapperResult->TryGetBoolField(TEXT("success"), bWrapperSuccess);
            ResultObj->SetBoolField(TEXT("success"), bWrapperSuccess);
            if (!bWrapperSuccess)
            {
                FString WrapperError;
                WrapperResult->TryGetStringField(TEXT("error"), WrapperError);
                ResultObj->SetStringField(
                    TEXT("error"),
                    WrapperError.IsEmpty() ? TEXT("Isolated Unreal Python execution failed") : WrapperError);
            }
            else if (ExitCode != 0)
            {
                ResultObj->SetBoolField(TEXT("success"), false);
                ResultObj->SetStringField(
                    TEXT("error"),
                    FString::Printf(TEXT("Isolated Unreal Python process exited with code %d"), ExitCode));
            }
        }
    }

    if (!ResultObj->HasField(TEXT("success")))
    {
        const bool bSucceeded = ExitCode == 0;
        ResultObj->SetBoolField(TEXT("success"), bSucceeded);
        if (!bSucceeded)
        {
            ResultObj->SetStringField(
                TEXT("error"),
                FString::Printf(TEXT("Isolated Unreal Python process exited with code %d"), ExitCode));
        }
    }

    if (ResultObj->GetBoolField(TEXT("success")))
    {
        ResultObj->SetStringField(TEXT("command_result"), TEXT("ok"));
        return ResultObj;
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleRunPython(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing parameters"));
    }

    bool bUnsafeInProcess = false;
    Params->TryGetBoolField(TEXT("unsafe_inprocess"), bUnsafeInProcess);
    if (!bUnsafeInProcess)
    {
        TSharedPtr<FJsonObject> ExecuteParams = MakeShared<FJsonObject>();
        CopyAllJsonFields_BP(Params, ExecuteParams);

        FString Code;
        if (ExecuteParams->TryGetStringField(TEXT("code"), Code) && !ExecuteParams->HasField(TEXT("python_script_or_patch")))
        {
            ExecuteParams->SetStringField(TEXT("python_script_or_patch"), Code);
        }

        TSharedPtr<FJsonObject> IsolatedResult = HandleExecuteUnrealPython(ExecuteParams);
        if (IsolatedResult.IsValid())
        {
            IsolatedResult->SetStringField(TEXT("invoked_via"), TEXT("run_python"));
        }
        return IsolatedResult;
    }

    if (Params->HasField(TEXT("timeout_seconds")))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("In-process run_python does not support timeout or forced cancellation. Remove 'unsafe_inprocess' to use isolated execution."));
    }

    FString Code;
    if (!Params->TryGetStringField(TEXT("code"), Code))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'code' parameter"));
    }

    IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
    if (!Python)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PythonScriptPlugin not available. Enable the PythonScriptPlugin."));
    }

    if (!Python->IsPythonAvailable())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Python is not available in this build. Enable Python support and restart the editor."));
    }

    FPythonCommandEx PyCmd;
    PyCmd.Command = Code;
    PyCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
    PyCmd.Flags = EPythonCommandFlags::Unattended;

    const bool bOk = Python->ExecPythonCommandEx(PyCmd);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("executed"), bOk);
    ResultObj->SetBoolField(TEXT("success"), bOk);
    ResultObj->SetStringField(TEXT("execution_backend"), TEXT("inprocess_unsafe"));
    if (!PyCmd.CommandResult.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("command_result"), PyCmd.CommandResult);
    }

    TArray<TSharedPtr<FJsonValue>> LogArray;
    for (const FPythonLogOutputEntry& Entry : PyCmd.LogOutput)
    {
        TSharedPtr<FJsonObject> LogEntryObj = MakeShared<FJsonObject>();
        LogEntryObj->SetStringField(TEXT("type"), LexToString(Entry.Type));
        LogEntryObj->SetStringField(TEXT("output"), Entry.Output);
        LogArray.Add(MakeShared<FJsonValueObject>(LogEntryObj));
    }
    if (LogArray.Num() > 0)
    {
        ResultObj->SetArrayField(TEXT("log_output"), LogArray);
    }

    if (!bOk)
    {
        ResultObj->SetStringField(TEXT("error"), TEXT("Python execution failed"));
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleExportBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString Target = ResolveBlueprintTargetParam(Params);
    if (Target.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target' parameter"));
    }

    TSharedPtr<IPlugin> ExportPlugin = IPluginManager::Get().FindPlugin(TEXT("ExportBlueprintToTxt"));
    if (!ExportPlugin.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ExportBlueprintToTxt plugin is not installed in this project"));
    }

    if (!ExportPlugin->IsEnabled())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ExportBlueprintToTxt plugin is installed but not enabled"));
    }

    FString ResolvedBlueprintPath;
    FString ExportError;
    if (!UExportBlueprintToTxtLibrary::ExportBlueprintAssetToText(Target, ResolvedBlueprintPath, ExportError))
    {
        if (!ExportError.IsEmpty())
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ExportError);
        }
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ExportBlueprintToTxt export failed"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    const bool bResolvedFolderTarget = !ResolvedBlueprintPath.Contains(TEXT("."));
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("target"), Target);
    ResultObj->SetStringField(TEXT("resolved_blueprint"), ResolvedBlueprintPath);
    ResultObj->SetStringField(TEXT("resolved_target"), ResolvedBlueprintPath);
    ResultObj->SetStringField(TEXT("target_type"), bResolvedFolderTarget ? TEXT("folder") : TEXT("asset"));
    ResultObj->SetStringField(TEXT("export_root"), FPaths::ProjectDir() / TEXT("ExportedBlueprints"));
    ResultObj->SetStringField(TEXT("export_plugin"), TEXT("ExportBlueprintToTxt"));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintMeta(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/asset_path/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FBlueprintMetaExportOptions ExportOptions;
    BuildMetaExportOptionsFromParams_BP(Params, ExportOptions);

    const FString MetaText = BuildBlueprintMetaText_BP(Blueprint, ExportOptions);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("isError"), false);
    ResultObj->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetArrayField(TEXT("content"), BuildMetaContentArray_BP(MetaText));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleExportBlueprintMeta(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/asset_path/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FBlueprintMetaExportOptions ExportOptions;
    BuildMetaExportOptionsFromParams_BP(Params, ExportOptions);
    const FString MetaText = BuildBlueprintMetaText_BP(Blueprint, ExportOptions);

    FString OutputPath;
    Params->TryGetStringField(TEXT("output_path"), OutputPath);
    if (OutputPath.IsEmpty())
    {
        Params->TryGetStringField(TEXT("output_file"), OutputPath);
    }
    if (OutputPath.IsEmpty())
    {
        Params->TryGetStringField(TEXT("path"), OutputPath);
    }

    FString OutputDirectory;
    if (OutputPath.IsEmpty())
    {
        Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);
        if (OutputDirectory.IsEmpty())
        {
            Params->TryGetStringField(TEXT("directory"), OutputDirectory);
        }
    }

    if (!OutputDirectory.IsEmpty())
    {
        OutputPath = FPaths::Combine(OutputDirectory, Blueprint->GetName() + TEXT(".meta"));
    }

    if (OutputPath.IsEmpty())
    {
        OutputPath = BuildDefaultMetaExportPath_BP(Blueprint);
    }
    else
    {
        if (!OutputPath.EndsWith(TEXT(".meta"), ESearchCase::IgnoreCase) && FPaths::GetExtension(OutputPath).IsEmpty())
        {
            OutputPath = FPaths::Combine(OutputPath, Blueprint->GetName() + TEXT(".meta"));
        }

        if (FPaths::IsRelative(OutputPath))
        {
            OutputPath = FPaths::Combine(FPaths::ProjectDir(), OutputPath);
        }
    }

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
    if (!FFileHelper::SaveStringToFile(MetaText, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to write meta file: %s"), *OutputPath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("isError"), false);
    ResultObj->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("output_path"), OutputPath);
    ResultObj->SetArrayField(TEXT("content"), BuildMetaContentArray_BP(MetaText));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleFetchBlueprintBestPractices(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> BestPractices;
    const TArray<FString> Tips = {
        TEXT("在修改 Blueprint 之前，先读取 Blueprint 属性、组件、函数和目标 graph，避免盲改。"),
        TEXT("优先用高层 `edit_blueprint` 和批量图编辑接口，而不是零散单节点操作。"),
        TEXT("一次只改一个明确 graph；复杂需求拆成多个 5 到 10 节点的小步骤。"),
        TEXT("EventGraph 负责流程编排，纯逻辑和可复用计算尽量下沉到函数。"),
        TEXT("新增变量时同时明确类型、是否实例可编辑、是否 ExposeOnSpawn、是否需要复制。"),
        TEXT("修改节点连接后始终编译 Blueprint，并把编译结果作为成功判定的一部分。"),
        TEXT("避免在 Tick 或高频路径中使用昂贵查找，例如 `Get All Actors Of Class`、重复 Dynamic Cast。"),
        TEXT("在批量改图前先确认 graph 名、node 唯一名和 pin 名，避免因为寻址不准导致误改。"),
        TEXT("优先保留现有 Blueprint 结构与命名风格，除非当前结构本身已经成为明显阻碍。"),
        TEXT("对复杂 Blueprint 任务，先产出结构化计划，再执行编辑，再做一次图结构审查。")
    };

    for (const FString& Tip : Tips)
    {
        BestPractices.Add(MakeShared<FJsonValueString>(Tip));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("category"), TEXT("blueprint_best_practices"));
    ResultObj->SetStringField(TEXT("summary"), TEXT("Blueprint 修改前，优先做上下文读取、分步改图、编译验证和高频路径性能检查。"));
    ResultObj->SetArrayField(TEXT("best_practices"), BestPractices);
    ResultObj->SetArrayField(TEXT("content"), BestPractices);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleBpAgent(const TSharedPtr<FJsonObject>& Params)
{
    FString RequestText;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("request_text"), RequestText))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'request_text' parameter"));
    }

    RequestText.TrimStartAndEndInline();
    if (RequestText.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'request_text' cannot be empty"));
    }

    FString RelevantDataError;
    TSharedPtr<FJsonObject> RelevantData = GetRelevantDataObject_BP(Params, RelevantDataError);
    if (!RelevantDataError.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(RelevantDataError);
    }

    FString BlueprintTarget = ResolveBlueprintTargetParam(Params);
    if (BlueprintTarget.IsEmpty())
    {
        BlueprintTarget = ResolveBlueprintTargetFromRelevantData_BP(RelevantData);
    }

    if (BlueprintTarget.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("bp_agent requires a Blueprint target. Provide target/blueprint_path/soft_path_from_project_root or relevant_data.blueprint_paths."));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintTarget);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintTarget));
    }

    const FString ShortDescription = GetFirstNonEmptyStringField_BP(Params, { TEXT("short_description") });
    const FString PreviousContext = GetFirstNonEmptyStringField_BP(Params, { TEXT("previous_context") });
    const FString PrimaryGraphName = ResolvePrimaryGraphName_BP(RelevantData);

    TSharedPtr<FJsonObject> BestPracticesResult = HandleFetchBlueprintBestPractices(MakeShared<FJsonObject>());

    TSharedPtr<FJsonObject> GraphAnalysisParams = MakeShared<FJsonObject>();
    GraphAnalysisParams->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    GraphAnalysisParams->SetStringField(TEXT("graph_name"), PrimaryGraphName);
    GraphAnalysisParams->SetBoolField(TEXT("include_pin_connections"), true);
    TSharedPtr<FJsonObject> GraphAnalysis = HandleAnalyzeBlueprintGraphFast(GraphAnalysisParams);

    const int32 ComponentCount = Blueprint->SimpleConstructionScript ? Blueprint->SimpleConstructionScript->GetAllNodes().Num() : 0;
    const int32 VariableCount = Blueprint->NewVariables.Num();
    const int32 FunctionCount = Blueprint->FunctionGraphs.Num();
    const int32 ImplementedInterfaceCount = Blueprint->ImplementedInterfaces.Num();
    const int32 DependencyCount = GetStringArrayField_BP(RelevantData, TEXT("dependencies")).Num();
    const int32 RelatedBlueprintCount = GetStringArrayField_BP(RelevantData, TEXT("blueprint_paths")).Num();

    TSharedPtr<FJsonObject> ContextSummary = MakeShared<FJsonObject>();
    ContextSummary->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ContextSummary->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ContextSummary->SetStringField(TEXT("primary_graph"), PrimaryGraphName);
    ContextSummary->SetNumberField(TEXT("component_count"), ComponentCount);
    ContextSummary->SetNumberField(TEXT("variable_count"), VariableCount);
    ContextSummary->SetNumberField(TEXT("function_count"), FunctionCount);
    ContextSummary->SetNumberField(TEXT("implemented_interface_count"), ImplementedInterfaceCount);
    ContextSummary->SetNumberField(TEXT("dependency_count"), DependencyCount);
    ContextSummary->SetNumberField(TEXT("related_blueprint_count"), RelatedBlueprintCount);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("agent"), TEXT("bp_agent"));
    ResultObj->SetStringField(TEXT("request_text"), RequestText);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("primary_graph"), PrimaryGraphName);
    ResultObj->SetObjectField(TEXT("context_summary"), ContextSummary);
    ResultObj->SetObjectField(TEXT("best_practices"), BestPracticesResult);
    ResultObj->SetObjectField(TEXT("graph_analysis"), GraphAnalysis);

    if (!ShortDescription.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("short_description"), ShortDescription);
    }

    if (!PreviousContext.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("previous_context"), PreviousContext);
    }

    TArray<TSharedPtr<FJsonValue>> ImportantContext;
    ImportantContext.Add(MakeShared<FJsonValueString>(TEXT("bp_agent 当前实现为结构化 Blueprint 工作流包装层，会复用现有 Blueprint 编辑、图分析和编译能力。")));
    ImportantContext.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("目标 Blueprint：%s"), *Blueprint->GetPathName())));
    ImportantContext.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("主 graph：%s"), *PrimaryGraphName)));
    ResultObj->SetArrayField(TEXT("important_context"), ImportantContext);

    TSharedPtr<FJsonObject> EditParams = MakeShared<FJsonObject>();
    CopyBlueprintEditFields_BP(RelevantData, EditParams);
    CopyBlueprintEditFields_BP(Params, EditParams);

    const bool bHasStructuredEdit =
        (HasActionableBlueprintEditFields_BP(RelevantData) || HasActionableBlueprintEditFields_BP(Params)) &&
        EditParams->Values.Num() > 0;

    if (bHasStructuredEdit)
    {
        EditParams->SetStringField(TEXT("target"), Blueprint->GetPathName());
        EditParams->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
        EditParams->SetStringField(TEXT("soft_path_from_project_root"), Blueprint->GetPathName());
        EditParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
        if (!EditParams->HasField(TEXT("compile_blueprint")))
        {
            EditParams->SetBoolField(TEXT("compile_blueprint"), true);
        }

        TSharedPtr<FJsonObject> EditResult = HandleEditBlueprint(EditParams);
        ResultObj->SetStringField(TEXT("agent_mode"), TEXT("edit_blueprint"));
        ResultObj->SetBoolField(TEXT("executed"), true);
        ResultObj->SetObjectField(TEXT("edit_result"), EditResult);
        ResultObj->SetBoolField(TEXT("success"), EditResult.IsValid() && EditResult->GetBoolField(TEXT("success")));

        if (EditResult.IsValid() && EditResult->HasTypedField<EJson::Object>(TEXT("compile_result")))
        {
            ResultObj->SetObjectField(TEXT("compile_result"), EditResult->GetObjectField(TEXT("compile_result")));
        }

        return ResultObj;
    }

    if (RequestTextLooksCompileOnly_BP(RequestText))
    {
        TSharedPtr<FJsonObject> CompileParams = MakeShared<FJsonObject>();
        CompileParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());

        TSharedPtr<FJsonObject> CompileResult = HandleCompileBlueprint(CompileParams);
        ResultObj->SetStringField(TEXT("agent_mode"), TEXT("compile_blueprint"));
        ResultObj->SetBoolField(TEXT("executed"), true);
        ResultObj->SetObjectField(TEXT("compile_result"), CompileResult);
        ResultObj->SetBoolField(TEXT("success"), CompileResult.IsValid() && CompileResult->GetBoolField(TEXT("success")));
        return ResultObj;
    }

    TArray<TSharedPtr<FJsonValue>> NextSteps;
    NextSteps.Add(MakeShared<FJsonValueString>(TEXT("先用 relevant_data 提供结构化编辑数据，例如 properties/components/variables/functions。")));
    NextSteps.Add(MakeShared<FJsonValueString>(TEXT("复杂图修改先调用 create_and_validate_blueprint_plan，再把计划拆成 edit_blueprint 或图节点批量操作。")));
    NextSteps.Add(MakeShared<FJsonValueString>(TEXT("如果当前只是想验证蓝图状态，可以直接请求编译。")));

    ResultObj->SetStringField(TEXT("agent_mode"), TEXT("analysis_only"));
    ResultObj->SetBoolField(TEXT("executed"), false);
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(
        TEXT("summary"),
        TEXT("未检测到可直接执行的结构化 Blueprint 编辑参数。bp_agent 已返回上下文摘要、最佳实践和 graph 分析，等待下一步编辑输入。"));
    ResultObj->SetArrayField(TEXT("next_steps"), NextSteps);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintPython(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/asset_path/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    const FString PythonText = BuildBlueprintPythonScript_BP(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("output_path"), BuildDefaultPythonExportPath_BP(Blueprint));
    ResultObj->SetStringField(TEXT("python_text"), PythonText);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleExportBlueprintPython(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/asset_path/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FString OutputPath;
    Params->TryGetStringField(TEXT("output_path"), OutputPath);
    if (OutputPath.IsEmpty())
    {
        Params->TryGetStringField(TEXT("output_file"), OutputPath);
    }
    if (OutputPath.IsEmpty())
    {
        Params->TryGetStringField(TEXT("path"), OutputPath);
    }

    FString BaseOutputDir = OutputPath;
    if (BaseOutputDir.IsEmpty())
    {
        BaseOutputDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("ExportedBlueprints"), TEXT("bpy"));
    }
    else
    {
        if (FPaths::IsRelative(BaseOutputDir))
        {
            BaseOutputDir = FPaths::Combine(FPaths::ProjectDir(), BaseOutputDir);
        }

        if (BaseOutputDir.EndsWith(TEXT(".py"), ESearchCase::IgnoreCase))
        {
            BaseOutputDir = FPaths::GetPath(BaseOutputDir);
        }
    }

    FString ExportError;
    if (!UBPDirectExporter::ExportBlueprintToPy(Blueprint->GetPathName(), BaseOutputDir, ExportError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            ExportError.IsEmpty() ? FString::Printf(TEXT("Failed to export blueprint python directory: %s"), *Blueprint->GetPathName()) : ExportError);
    }

    const FString BlueprintName = Blueprint->GetName();
    const FString ExportDir = FPaths::Combine(BaseOutputDir, BlueprintName);
    const FString MainFilePath = FPaths::Combine(ExportDir, TEXT("__bp__.bp.py"));

    FString MainFileText;
    FFileHelper::LoadFileToString(MainFileText, *MainFilePath);

    TArray<FString> ExportedFiles;
    IFileManager::Get().FindFilesRecursive(ExportedFiles, *ExportDir, TEXT("*"), true, false, false);
    ExportedFiles.Sort();

    TArray<TSharedPtr<FJsonValue>> ExportedFileValues;
    ExportedFileValues.Reserve(ExportedFiles.Num());
    for (const FString& FilePath : ExportedFiles)
    {
        ExportedFileValues.Add(MakeShared<FJsonValueString>(FilePath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("output_dir"), ExportDir);
    ResultObj->SetStringField(TEXT("output_path"), MainFilePath);
    ResultObj->SetStringField(TEXT("format"), TEXT("bpy_directory"));
    ResultObj->SetStringField(TEXT("producer"), TEXT("ExportBpy"));
    ResultObj->SetArrayField(TEXT("exported_files"), ExportedFileValues);
    if (!MainFileText.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("python_text"), MainFileText);
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintBpy(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/asset_path/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FString BpyText;
    FString ExportError;
    if (!UBPDirectExporter::ReadBlueprintToBpyText(Blueprint->GetPathName(), BpyText, ExportError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            ExportError.IsEmpty() ? FString::Printf(TEXT("Failed to generate bpy text for %s"), *Blueprint->GetPathName()) : ExportError);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("output_path"), BuildDefaultBpyExportPath_BP(Blueprint));
    ResultObj->SetStringField(TEXT("bpy_text"), BpyText);
    ResultObj->SetStringField(TEXT("python_text"), BpyText);
    ResultObj->SetStringField(TEXT("format"), TEXT("bpy"));
    ResultObj->SetStringField(TEXT("producer"), TEXT("ExportBpy"));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleExportBlueprintBpy(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/asset_path/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FString OutputPath;
    Params->TryGetStringField(TEXT("output_path"), OutputPath);
    if (OutputPath.IsEmpty())
    {
        Params->TryGetStringField(TEXT("output_file"), OutputPath);
    }
    if (OutputPath.IsEmpty())
    {
        Params->TryGetStringField(TEXT("path"), OutputPath);
    }

    if (OutputPath.IsEmpty())
    {
        OutputPath = BuildDefaultBpyExportPath_BP(Blueprint);
    }
    else
    {
        if (!OutputPath.EndsWith(TEXT(".bp.py"), ESearchCase::IgnoreCase) && FPaths::GetExtension(OutputPath).IsEmpty())
        {
            OutputPath = FPaths::Combine(OutputPath, Blueprint->GetName() + TEXT(".bp.py"));
        }

        if (FPaths::IsRelative(OutputPath))
        {
            OutputPath = FPaths::Combine(FPaths::ProjectDir(), OutputPath);
        }
    }

    FString ExportError;
    if (!UBPDirectExporter::ExportBlueprintToBpyFile(Blueprint->GetPathName(), OutputPath, ExportError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            ExportError.IsEmpty() ? FString::Printf(TEXT("Failed to export bpy file: %s"), *OutputPath) : ExportError);
    }

    FString BpyText;
    FString ReadError;
    UBPDirectExporter::ReadBlueprintToBpyText(Blueprint->GetPathName(), BpyText, ReadError);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("output_path"), OutputPath);
    ResultObj->SetStringField(TEXT("format"), TEXT("bpy"));
    ResultObj->SetStringField(TEXT("producer"), TEXT("ExportBpy"));
    if (!BpyText.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("bpy_text"), BpyText);
        ResultObj->SetStringField(TEXT("python_text"), BpyText);
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleImportBlueprintPython(const TSharedPtr<FJsonObject>& Params)
{
    FString InputPath = GetFirstNonEmptyStringField_BP(Params, {TEXT("input_path"), TEXT("script_path")});
    if (!InputPath.IsEmpty())
    {
        if (FPaths::IsRelative(InputPath))
        {
            InputPath = FPaths::Combine(FPaths::ProjectDir(), InputPath);
        }

        const FString CleanFilename = FPaths::GetCleanFilename(InputPath);
        const bool bIsUpperPackagePath =
            CleanFilename.Equals(TEXT("__upper__.py"), ESearchCase::IgnoreCase) ||
            (IFileManager::Get().DirectoryExists(*InputPath) &&
                IFileManager::Get().FileExists(*FPaths::Combine(InputPath, TEXT("__upper__.py"))));
        const bool bIsCanonicalBpyDslPath =
            CleanFilename.Equals(TEXT("__bp__.bp.py"), ESearchCase::IgnoreCase) ||
            CleanFilename.EndsWith(TEXT(".bp.py"), ESearchCase::IgnoreCase) ||
            (IFileManager::Get().DirectoryExists(*InputPath) &&
                IFileManager::Get().FileExists(*FPaths::Combine(InputPath, TEXT("__bp__.bp.py"))));
        const bool bIsBpyDslPath =
            bIsUpperPackagePath || bIsCanonicalBpyDslPath;

        if (bIsBpyDslPath)
        {
            const FString TargetBlueprintRef = ResolveBlueprintTargetParam(Params);
            if (TargetBlueprintRef.IsEmpty())
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing target blueprint path for bpy import"));
            }

            bool bOverwrite = true;
            Params->TryGetBoolField(TEXT("overwrite"), bOverwrite);

            UBlueprint* ExistingBlueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(TargetBlueprintRef);
            if (!bOverwrite && ExistingBlueprint)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Target blueprint already exists: %s"), *TargetBlueprintRef));
            }

            if (bOverwrite && ExistingBlueprint)
            {
                const FString ExistingAssetPath = FPackageName::ObjectPathToPackageName(ExistingBlueprint->GetPathName());
                if (!ExistingAssetPath.IsEmpty() &&
                    UEditorAssetLibrary::DoesAssetExist(ExistingAssetPath) &&
                    !UEditorAssetLibrary::DeleteAsset(ExistingAssetPath))
                {
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Failed to delete existing blueprint before bpy import: %s"), *ExistingAssetPath));
                }
            }

            TSharedPtr<IPlugin> ExportBpyPlugin = IPluginManager::Get().FindPlugin(TEXT("ExportBpy"));
            if (!ExportBpyPlugin.IsValid())
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ExportBpy plugin is not available"));
            }

            const FString ScriptId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            const FString CacheRoot = FPaths::Combine(BuildPythonExecutionCacheRoot_BP(), TEXT("ImportBpy"), ScriptId);
            IFileManager::Get().MakeDirectory(*CacheRoot, true);

            const FString PluginPythonDir = NormalizePythonPathForScriptLiteral_BP(
                FPaths::ConvertRelativePathToFull(FPaths::Combine(ExportBpyPlugin->GetBaseDir(), TEXT("Content"), TEXT("Python"))));
            const FString SourcePathLiteral = NormalizePythonPathForScriptLiteral_BP(FPaths::ConvertRelativePathToFull(InputPath));
            const FString ResultPath = FPaths::Combine(CacheRoot, TEXT("result.json"));
            const FString ResultPathLiteral = NormalizePythonPathForScriptLiteral_BP(FPaths::ConvertRelativePathToFull(ResultPath));
            const FString WrapperPath = FPaths::Combine(CacheRoot, TEXT("import_bpy_wrapper.py"));
            bool bCompileBlueprint = true;
            Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

            const FString WrapperScript = FString::Printf(
                TEXT("import json\n")
                TEXT("import os\n")
                TEXT("import sys\n")
                TEXT("plugin_py = r'''%s'''\n")
                TEXT("source_path = r'''%s'''\n")
                TEXT("target_path = r'''%s'''\n")
                TEXT("result_path = r'''%s'''\n")
                TEXT("compile_blueprint = %s\n")
                TEXT("use_upper_compiler = %s\n")
                TEXT("if plugin_py not in sys.path:\n")
                TEXT("    sys.path.insert(0, plugin_py)\n")
                TEXT("import importlib\n")
                TEXT("importlib.invalidate_caches()\n")
                TEXT("if 'ue_bp_dsl.core' in sys.modules:\n")
                TEXT("    importlib.reload(sys.modules['ue_bp_dsl.core'])\n")
                TEXT("if 'ue_bp_dsl' in sys.modules:\n")
                TEXT("    importlib.reload(sys.modules['ue_bp_dsl'])\n")
                TEXT("if use_upper_compiler:\n")
                TEXT("    from bpy_compile import api as bpy_compile_api\n")
                TEXT("    bpy_compile_api = importlib.reload(bpy_compile_api)\n")
                TEXT("    compile_source = source_path if os.path.isdir(source_path) else os.path.dirname(source_path)\n")
                TEXT("    ok, err = bpy_compile_api.compile_and_import(compile_source, target_path=target_path, compile_asset=compile_blueprint)\n")
                TEXT("    import_mode = 'upper_package'\n")
                TEXT("else:\n")
                TEXT("    import bp_importer\n")
                TEXT("    bp_importer = importlib.reload(bp_importer)\n")
                TEXT("    ok, err = bp_importer.import_path(source_path, target_path, compile_blueprint=compile_blueprint)\n")
                TEXT("    import_mode = 'bpy_directory'\n")
                TEXT("result = {\n")
                TEXT("    'success': bool(ok),\n")
                TEXT("    'error': '' if ok else str(err),\n")
                TEXT("    'source_path': source_path,\n")
                TEXT("    'target_blueprint': target_path,\n")
                TEXT("    'import_mode': import_mode,\n")
                TEXT("}\n")
                TEXT("with open(result_path, 'w', encoding='utf-8') as handle:\n")
                TEXT("    json.dump(result, handle, ensure_ascii=False, indent=2)\n")
                TEXT("if not ok:\n")
                TEXT("    raise RuntimeError(str(err))\n"),
                *PluginPythonDir,
                *SourcePathLiteral,
                *TargetBlueprintRef,
                *ResultPathLiteral,
                bCompileBlueprint ? TEXT("True") : TEXT("False"),
                bIsUpperPackagePath ? TEXT("True") : TEXT("False"));

            if (!FFileHelper::SaveStringToFile(WrapperScript, *WrapperPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to write bpy import wrapper script: %s"), *WrapperPath));
            }

            TSharedPtr<FJsonObject> RunParams = MakeShared<FJsonObject>();
            RunParams->SetStringField(TEXT("code"), WrapperPath);
            RunParams->SetBoolField(TEXT("unsafe_inprocess"), true);

            TSharedPtr<FJsonObject> RunResult = HandleRunPython(RunParams);

            FString ImportResultJson;
            if (!FFileHelper::LoadFileToString(ImportResultJson, *ResultPath))
            {
                if (RunResult.IsValid())
                {
                    return RunResult;
                }
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to read bpy import result"));
            }

            TSharedPtr<FJsonObject> ImportResult;
            if (!TryDeserializeJsonObject_BP(ImportResultJson, ImportResult) || !ImportResult.IsValid())
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to parse bpy import result JSON"));
            }

            bool bSuccess = false;
            ImportResult->TryGetBoolField(TEXT("success"), bSuccess);
            if (!bSuccess)
            {
                FString ImportError;
                ImportResult->TryGetStringField(TEXT("error"), ImportError);
                if (ImportError.IsEmpty() && RunResult.IsValid())
                {
                    RunResult->TryGetStringField(TEXT("error"), ImportError);
                }
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(
                        TEXT("bpy import failed (%s -> %s)%s%s"),
                        *InputPath,
                        *TargetBlueprintRef,
                        ImportError.IsEmpty() ? TEXT("") : TEXT(": "),
                        *ImportError));
            }

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetBoolField(TEXT("success"), true);
            ResultObj->SetStringField(TEXT("input_path"), InputPath);
            ResultObj->SetStringField(TEXT("target_blueprint"), TargetBlueprintRef);
            FString ImportMode = TEXT("bpy_directory");
            ImportResult->TryGetStringField(TEXT("import_mode"), ImportMode);
            ResultObj->SetStringField(TEXT("import_mode"), ImportMode);
            ResultObj->SetBoolField(TEXT("compiled"), true);
            if (RunResult.IsValid())
            {
                ResultObj->SetObjectField(TEXT("python_run_result"), RunResult);
            }
            return ResultObj;
        }
    }

    TSharedPtr<FJsonObject> Spec;
    FString ExtractError;
    if (!ExtractPythonImportSpec_BP(Params, Spec, ExtractError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ExtractError);
    }

    FString TargetBlueprintRef = ResolveBlueprintTargetParam(Params);
    if (TargetBlueprintRef.IsEmpty() && Spec.IsValid())
    {
        TargetBlueprintRef = GetFirstNonEmptyStringField_BP(
            Spec,
            {TEXT("target_blueprint"), TEXT("path"), TEXT("object_path"), TEXT("source_blueprint"), TEXT("package_path")});
    }

    if (TargetBlueprintRef.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing target blueprint path"));
    }

    bool bOverwrite = true;
    Params->TryGetBoolField(TEXT("overwrite"), bOverwrite);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(TargetBlueprintRef);
    if (!Blueprint)
    {
        FString CreateError;
        Blueprint = CreateBlueprintAssetForPythonImport_BP(TargetBlueprintRef, ResolveParentClassForPythonImport_BP(Spec), CreateError);
        if (!Blueprint)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(CreateError);
        }
    }
    else if (!bOverwrite)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Target blueprint already exists: %s"), *Blueprint->GetPathName()));
    }

    ApplyInterfacesForPythonImport_BP(Blueprint, Spec);

    TSharedPtr<FJsonObject> EditParams = MakeShared<FJsonObject>();
    EditParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
    EditParams->SetBoolField(TEXT("compile_blueprint"), false);
    EditParams->SetBoolField(TEXT("continue_on_error"), false);

    const TSharedPtr<FJsonObject>* BlueprintProperties = nullptr;
    if (Spec->TryGetObjectField(TEXT("blueprint_properties"), BlueprintProperties) && BlueprintProperties && BlueprintProperties->IsValid())
    {
        EditParams->SetObjectField(TEXT("blueprint_properties"), *BlueprintProperties);
    }

    const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
    if (Spec->TryGetArrayField(TEXT("components"), Components) && Components)
    {
        EditParams->SetArrayField(TEXT("components"), *Components);
    }

    const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
    if (Spec->TryGetArrayField(TEXT("variables"), Variables) && Variables)
    {
        EditParams->SetArrayField(TEXT("variables"), *Variables);
    }

    TSharedPtr<FJsonObject> EditResult = HandleEditBlueprint(EditParams);
    if (!EditResult.IsValid() || !EditResult->GetBoolField(TEXT("success")))
    {
        return EditResult.IsValid()
            ? EditResult
            : FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to apply blueprint properties/components/variables"));
    }

    int32 ImportedGraphCount = 0;
    const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
    if (Spec->TryGetArrayField(TEXT("graphs"), Graphs) && Graphs)
    {
        for (int32 GraphIndex = 0; GraphIndex < Graphs->Num(); ++GraphIndex)
        {
            const TSharedPtr<FJsonValue>& GraphValue = (*Graphs)[GraphIndex];
            if (!GraphValue.IsValid() || GraphValue->Type != EJson::Object)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("graphs[%d] must be an object"), GraphIndex));
            }

            const TSharedPtr<FJsonObject> GraphSpec = GraphValue->AsObject();
            FString GraphName;
            FString GraphKind = TEXT("function");
            FString NodeText;
            bool bClearGraph = true;

            GraphSpec->TryGetStringField(TEXT("graph_name"), GraphName);
            GraphSpec->TryGetStringField(TEXT("graph_kind"), GraphKind);
            GraphSpec->TryGetStringField(TEXT("node_text"), NodeText);
            GraphSpec->TryGetBoolField(TEXT("clear_graph"), bClearGraph);

            if (GraphName.IsEmpty())
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("graphs[%d] is missing graph_name"), GraphIndex));
            }

            if (!ResolveGraphForPythonImport_BP(Blueprint, GraphName, GraphKind))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to resolve target graph: %s"), *GraphName));
            }

            if (!UExportBlueprintToTxtLibrary::ImportNodesFromText(Blueprint, GraphName, NodeText, bClearGraph))
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to import graph text for graph: %s"), *GraphName));
            }

            ++ImportedGraphCount;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    if (bCompileBlueprint)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("target_blueprint"), Blueprint->GetPathName());
    ResultObj->SetBoolField(TEXT("compiled"), bCompileBlueprint);
    ResultObj->SetNumberField(TEXT("imported_graph_count"), ImportedGraphCount);
    ResultObj->SetObjectField(TEXT("edit_result"), EditResult);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleExportBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
    const FString Target = ResolveBlueprintTargetParam(Params);
    if (Target.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target' parameter"));
    }

    FString OutputDirectory;
    if (!Params->TryGetStringField(TEXT("output_directory"), OutputDirectory))
    {
        Params->TryGetStringField(TEXT("directory"), OutputDirectory);
    }

    FString ResolvedBlueprintPath;
    FString ResolvedDirectory;
    FString ManifestPath;
    FString ExportError;
    int32 ExportedFunctionCount = 0;

    if (!UExportBlueprintToTxtLibrary::ExportBlueprintFunctionsToDirectory(
            Target,
            OutputDirectory,
            ResolvedBlueprintPath,
            ResolvedDirectory,
            ManifestPath,
            ExportedFunctionCount,
            ExportError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            ExportError.IsEmpty() ? TEXT("Failed to export blueprint functions") : ExportError);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("target"), Target);
    ResultObj->SetStringField(TEXT("resolved_blueprint"), ResolvedBlueprintPath);
    ResultObj->SetStringField(TEXT("export_directory"), ResolvedDirectory);
    ResultObj->SetStringField(TEXT("manifest_path"), ManifestPath);
    ResultObj->SetNumberField(TEXT("exported_function_count"), ExportedFunctionCount);
    ResultObj->SetStringField(TEXT("export_plugin"), TEXT("ExportBlueprintToTxt"));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleImportBlueprintFunctions(const TSharedPtr<FJsonObject>& Params)
{
    FString InputDirectory;
    if (!Params->TryGetStringField(TEXT("input_directory"), InputDirectory))
    {
        Params->TryGetStringField(TEXT("directory"), InputDirectory);
    }

    if (InputDirectory.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'input_directory' parameter"));
    }

    const FString Target = ResolveBlueprintTargetParam(Params);

    FString ResolvedBlueprintPath;
    FString ImportError;
    int32 ImportedFunctionCount = 0;
    if (!UExportBlueprintToTxtLibrary::ImportBlueprintFunctionsFromDirectory(
            InputDirectory,
            Target,
            ResolvedBlueprintPath,
            ImportedFunctionCount,
            ImportError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            ImportError.IsEmpty() ? TEXT("Failed to import blueprint functions") : ImportError);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("input_directory"), InputDirectory);
    ResultObj->SetStringField(TEXT("resolved_blueprint"), ResolvedBlueprintPath);
    ResultObj->SetNumberField(TEXT("imported_function_count"), ImportedFunctionCount);
    ResultObj->SetStringField(TEXT("export_plugin"), TEXT("ExportBlueprintToTxt"));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleTriggerLiveCoding(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid() || !Params->HasField(TEXT("toggle")))
    {
        TSharedPtr<FJsonObject> CompileParams = MakeShared<FJsonObject>();
        if (Params.IsValid())
        {
            CompileParams->Values = Params->Values;
        }

        CompileParams->SetBoolField(TEXT("wait"), true);
        if (!CompileParams->HasField(TEXT("show_console")))
        {
            CompileParams->SetBoolField(TEXT("show_console"), true);
        }
        if (!CompileParams->HasField(TEXT("enable_if_needed")))
        {
            CompileParams->SetBoolField(TEXT("enable_if_needed"), true);
        }

        TSharedPtr<FJsonObject> ResultObj = HandleCompileLiveCoding(CompileParams);
        if (ResultObj.IsValid())
        {
            ResultObj->SetStringField(TEXT("command"), TEXT("trigger_live_coding"));

            FString Explanation;
            if (Params.IsValid() && Params->TryGetStringField(TEXT("explanation"), Explanation))
            {
                ResultObj->SetStringField(TEXT("explanation"), Explanation);
            }
        }

        return ResultObj;
    }

    bool bEnable = false;
    FString ParseError;
    if (!TryParseFlexibleBoolField(Params, TEXT("toggle"), bEnable, ParseError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
    }

#if PLATFORM_WINDOWS
    ILiveCodingModule* LiveCodingModule = FModuleManager::LoadModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
    if (!LiveCodingModule)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("LiveCoding module is not available"));
    }

    if (bEnable && !LiveCodingModule->CanEnableForSession())
    {
        const FString EnableError = LiveCodingModule->GetEnableErrorText().ToString();
        if (!EnableError.IsEmpty())
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Cannot enable Live Coding: %s"), *EnableError));
        }
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot enable Live Coding in current session"));
    }

    LiveCodingModule->EnableByDefault(bEnable);
    LiveCodingModule->EnableForSession(bEnable);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("command"), TEXT("trigger_live_coding"));
    ResultObj->SetBoolField(TEXT("toggle"), bEnable);
    ResultObj->SetBoolField(TEXT("enabled_by_default"), LiveCodingModule->IsEnabledByDefault());
    ResultObj->SetBoolField(TEXT("enabled_for_session"), LiveCodingModule->IsEnabledForSession());
    ResultObj->SetBoolField(TEXT("has_started"), LiveCodingModule->HasStarted());
    ResultObj->SetBoolField(TEXT("can_enable_for_session"), LiveCodingModule->CanEnableForSession());
    ResultObj->SetStringField(TEXT("enable_error_text"), LiveCodingModule->GetEnableErrorText().ToString());
    return ResultObj;
#else
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("trigger_live_coding is only supported on Windows editor builds"));
#endif
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCompileLiveCoding(const TSharedPtr<FJsonObject>& Params)
{
#if PLATFORM_WINDOWS
    ILiveCodingModule* LiveCodingModule = FModuleManager::LoadModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
    if (!LiveCodingModule)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("LiveCoding module is not available"));
    }

    bool bWaitForCompletion = true;
    bool bShowConsole = true;
    bool bEnableIfNeeded = true;
    FString ParseError;

    if (Params.IsValid() && Params->HasField(TEXT("wait")) &&
        !TryParseFlexibleBoolField(Params, TEXT("wait"), bWaitForCompletion, ParseError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
    }

    if (Params.IsValid() && Params->HasField(TEXT("show_console")) &&
        !TryParseFlexibleBoolField(Params, TEXT("show_console"), bShowConsole, ParseError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
    }

    if (Params.IsValid() && Params->HasField(TEXT("enable_if_needed")) &&
        !TryParseFlexibleBoolField(Params, TEXT("enable_if_needed"), bEnableIfNeeded, ParseError))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ParseError);
    }

    if (bEnableIfNeeded && !LiveCodingModule->IsEnabledForSession())
    {
        if (!LiveCodingModule->CanEnableForSession())
        {
            const FString EnableError = LiveCodingModule->GetEnableErrorText().ToString();
            if (!EnableError.IsEmpty())
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Cannot enable Live Coding: %s"), *EnableError));
            }

            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot enable Live Coding in current session"));
        }

        LiveCodingModule->EnableForSession(true);
    }

    if (!LiveCodingModule->IsEnabledForSession())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Live Coding is not enabled for this session"));
    }

    if (bShowConsole)
    {
        LiveCodingModule->ShowConsole();
    }

    const ELiveCodingCompileFlags CompileFlags = bWaitForCompletion
        ? ELiveCodingCompileFlags::WaitForCompletion
        : ELiveCodingCompileFlags::None;

    ELiveCodingCompileResult CompileResult = ELiveCodingCompileResult::Failure;
    const bool bCompileCallSucceeded = LiveCodingModule->Compile(CompileFlags, &CompileResult);
    const bool bSuccess =
        CompileResult == ELiveCodingCompileResult::Success ||
        CompileResult == ELiveCodingCompileResult::NoChanges ||
        (!bWaitForCompletion && CompileResult == ELiveCodingCompileResult::InProgress);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), bSuccess);
    ResultObj->SetBoolField(TEXT("compile_call_succeeded"), bCompileCallSucceeded);
    ResultObj->SetBoolField(TEXT("wait"), bWaitForCompletion);
    ResultObj->SetBoolField(TEXT("show_console"), bShowConsole);
    ResultObj->SetBoolField(TEXT("enable_if_needed"), bEnableIfNeeded);
    ResultObj->SetStringField(TEXT("compile_result"), LiveCodingCompileResultToString(CompileResult));
    ResultObj->SetBoolField(TEXT("enabled_for_session"), LiveCodingModule->IsEnabledForSession());
    ResultObj->SetBoolField(TEXT("has_started"), LiveCodingModule->HasStarted());
    ResultObj->SetBoolField(TEXT("is_compiling"), LiveCodingModule->IsCompiling());
    ResultObj->SetStringField(TEXT("enable_error_text"), LiveCodingModule->GetEnableErrorText().ToString());
    return ResultObj;
#else
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("compile_live_coding is only supported on Windows editor builds"));
#endif
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetLiveCodingLog(const TSharedPtr<FJsonObject>& Params)
{
#if PLATFORM_WINDOWS
    int32 MaxLines = 200;
    double NumberValue = 0.0;
    if (Params.IsValid() && Params->TryGetNumberField(TEXT("max_lines"), NumberValue))
    {
        MaxLines = FMath::Clamp(static_cast<int32>(NumberValue), 20, 2000);
    }

    const TArray<FLiveCodingLogFileCandidate> Candidates = GetRecentProjectLogFiles();
    for (const FLiveCodingLogFileCandidate& Candidate : Candidates)
    {
        TArray<FString> LogLines;
        int32 StartLineNumber = 1;
        FString Summary;
        if (!ExtractLatestLiveCodingBlock(Candidate.Path, MaxLines, LogLines, StartLineNumber, Summary))
        {
            continue;
        }

        TArray<TSharedPtr<FJsonValue>> JsonLines;
        JsonLines.Reserve(LogLines.Num());
        for (const FString& Line : LogLines)
        {
            JsonLines.Add(MakeShared<FJsonValueString>(Line));
        }

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetBoolField(TEXT("success"), true);
        ResultObj->SetStringField(TEXT("command"), TEXT("get_live_coding_log"));
        ResultObj->SetStringField(TEXT("log_file"), Candidate.Path);
        ResultObj->SetStringField(TEXT("summary"), Summary);
        ResultObj->SetNumberField(TEXT("start_line_number"), StartLineNumber);
        ResultObj->SetNumberField(TEXT("line_count"), LogLines.Num());
        ResultObj->SetArrayField(TEXT("lines"), JsonLines);
        ResultObj->SetStringField(TEXT("log_text"), FString::Join(LogLines, TEXT("\n")));
        return ResultObj;
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No Live Coding log block was found in Saved/Logs"));
#else
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_live_coding_log is only supported on Windows editor builds"));
#endif
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetGameModeDefaultSpawnClass(const TSharedPtr<FJsonObject>& Params)
{
    FString GameModeBlueprintRef;
    Params->TryGetStringField(TEXT("game_mode_blueprint"), GameModeBlueprintRef);
    if (GameModeBlueprintRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("game_mode_blueprint_path"), GameModeBlueprintRef);
    }
    if (GameModeBlueprintRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("blueprint_path"), GameModeBlueprintRef);
    }
    if (GameModeBlueprintRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("blueprint_name"), GameModeBlueprintRef);
    }
    if (GameModeBlueprintRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("game_mode"), GameModeBlueprintRef);
    }
    if (GameModeBlueprintRef.IsEmpty())
    {
        GameModeBlueprintRef = TEXT("/Game/Blueprints/GM_Sandbox");
    }

    FString SpawnClassRef;
    Params->TryGetStringField(TEXT("spawn_class"), SpawnClassRef);
    if (SpawnClassRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("default_spawn_class"), SpawnClassRef);
    }
    if (SpawnClassRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("pawn_class"), SpawnClassRef);
    }
    if (SpawnClassRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("default_pawn_class"), SpawnClassRef);
    }
    if (SpawnClassRef.IsEmpty())
    {
        Params->TryGetStringField(TEXT("class_path"), SpawnClassRef);
    }

    if (SpawnClassRef.IsEmpty())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("Missing spawn class parameter (spawn_class/default_spawn_class/pawn_class/default_pawn_class/class_path)"));
    }

    UBlueprint* GameModeBlueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(GameModeBlueprintRef);
    if (!GameModeBlueprint)
    {
        GameModeBlueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(GameModeBlueprintRef));
    }
    if (!GameModeBlueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("GameMode blueprint not found: %s"), *GameModeBlueprintRef));
    }

    UClass* GameModeClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(GameModeBlueprint);
    if (!GameModeClass || !GameModeClass->IsChildOf(AGameModeBase::StaticClass()))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint is not a GameModeBase subclass: %s"), *GameModeBlueprint->GetPathName()));
    }

    UClass* SpawnClass = ResolveClassByReference(SpawnClassRef);
    if (!SpawnClass)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to resolve spawn class: %s"), *SpawnClassRef));
    }

    if (!SpawnClass->IsChildOf(APawn::StaticClass()))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Spawn class must derive from APawn: %s"), *SpawnClass->GetPathName()));
    }

    UObject* GameModeCDO = ResolveBlueprintEndpointObject(GameModeBlueprint, TEXT(""));
    if (!GameModeCDO)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to resolve GameMode default object"));
    }

    FProperty* DefaultPawnProperty = FindPropertyByNameLoose(GameModeCDO->GetClass(), TEXT("DefaultPawnClass"));
    FClassProperty* ClassProperty = CastField<FClassProperty>(DefaultPawnProperty);
    if (!ClassProperty)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("DefaultPawnClass property not found on GameMode blueprint"));
    }

    if (!SpawnClass->IsChildOf(ClassProperty->MetaClass))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Resolved class '%s' is not assignable to DefaultPawnClass"), *SpawnClass->GetPathName()));
    }

    UObject* ExistingValue = ClassProperty->GetObjectPropertyValue_InContainer(GameModeCDO);
    UClass* PreviousClass = Cast<UClass>(ExistingValue);

    GameModeCDO->Modify();
    ClassProperty->SetObjectPropertyValue_InContainer(GameModeCDO, SpawnClass);

    FBlueprintEditorUtils::MarkBlueprintAsModified(GameModeBlueprint);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);
    if (bCompileBlueprint)
    {
        FKismetEditorUtilities::CompileBlueprint(GameModeBlueprint);
    }

    bool bSaveBlueprint = false;
    Params->TryGetBoolField(TEXT("save_blueprint"), bSaveBlueprint);
    bool bSaved = false;
    if (bSaveBlueprint)
    {
        bSaved = UEditorAssetLibrary::SaveLoadedAsset(GameModeBlueprint);
        if (!bSaved)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to save blueprint: %s"), *GameModeBlueprint->GetPathName()));
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("command"), TEXT("set_gamemode_default_spawn_class"));
    ResultObj->SetStringField(TEXT("game_mode_blueprint"), GameModeBlueprint->GetPathName());
    ResultObj->SetStringField(TEXT("property"), TEXT("DefaultPawnClass"));
    ResultObj->SetStringField(TEXT("previous_class_name"), PreviousClass ? PreviousClass->GetName() : TEXT("None"));
    ResultObj->SetStringField(TEXT("previous_class_path"), PreviousClass ? PreviousClass->GetPathName() : TEXT(""));
    ResultObj->SetStringField(TEXT("spawn_class_name"), SpawnClass->GetName());
    ResultObj->SetStringField(TEXT("spawn_class_path"), SpawnClass->GetPathName());
    ResultObj->SetBoolField(TEXT("compiled"), bCompileBlueprint);
    ResultObj->SetBoolField(TEXT("saved"), bSaved);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetMeshMaterialColor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    // Try to cast to StaticMeshComponent or PrimitiveComponent
    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Get color parameter
    TArray<float> ColorArray;
    const TArray<TSharedPtr<FJsonValue>>* ColorJsonArray;
    if (!Params->TryGetArrayField(TEXT("color"), ColorJsonArray) || ColorJsonArray->Num() != 4)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'color' must be an array of 4 float values [R, G, B, A]"));
    }

    for (const TSharedPtr<FJsonValue>& Value : *ColorJsonArray)
    {
        ColorArray.Add(FMath::Clamp(Value->AsNumber(), 0.0f, 1.0f));
    }

    FLinearColor Color(ColorArray[0], ColorArray[1], ColorArray[2], ColorArray[3]);

    // Get material slot index
    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Get parameter name
    FString ParameterName = TEXT("BaseColor");
    Params->TryGetStringField(TEXT("parameter_name"), ParameterName);

    // Get or create material
    UMaterialInterface* Material = nullptr;
    
    // Check if a specific material path was provided
    FString MaterialPath;
    if (Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (!Material)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
        }
    }
    else
    {
        // Use existing material on the component
        Material = PrimComponent->GetMaterial(MaterialSlot);
        if (!Material)
        {
            // Try to use a default material
            Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(TEXT("/Engine/BasicShapes/BasicShapeMaterial")));
            if (!Material)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No material found on component and failed to load default material"));
            }
        }
    }

    // Create a dynamic material instance
    UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(Material, PrimComponent);
    if (!DynMaterial)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create dynamic material instance"));
    }

    // Set the color parameter
    DynMaterial->SetVectorParameterValue(*ParameterName, Color);

    // Apply the material to the component
    PrimComponent->SetMaterial(MaterialSlot, DynMaterial);

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Log success
    UE_LOG(LogTemp, Log, TEXT("Successfully set material color on component %s: R=%f, G=%f, B=%f, A=%f"), 
        *ComponentName, Color.R, Color.G, Color.B, Color.A);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetStringField(TEXT("parameter_name"), ParameterName);
    
    TArray<TSharedPtr<FJsonValue>> ColorResultArray;
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.R));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.G));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.B));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.A));
    ResultObj->SetArrayField(TEXT("color"), ColorResultArray);
    
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetAvailableMaterials(const TSharedPtr<FJsonObject>& Params)
{
    // Get parameters - make search path completely dynamic
    FString SearchPath;
    if (!Params->TryGetStringField(TEXT("search_path"), SearchPath))
    {
        // Default to empty string to search everywhere
        SearchPath = TEXT("");
    }
    
    bool bIncludeEngineMaterials = true;
    if (Params->HasField(TEXT("include_engine_materials")))
    {
        bIncludeEngineMaterials = Params->GetBoolField(TEXT("include_engine_materials"));
    }

    // Get asset registry module
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Create filter for materials
    FARFilter Filter;
    Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialInstanceDynamic::StaticClass()->GetClassPathName());
    
    // Add search paths dynamically
    if (!SearchPath.IsEmpty())
    {
        // Ensure the path starts with /
        if (!SearchPath.StartsWith(TEXT("/")))
        {
            SearchPath = TEXT("/") + SearchPath;
        }
        // Ensure the path ends with / for proper directory search
        if (!SearchPath.EndsWith(TEXT("/")))
        {
            SearchPath += TEXT("/");
        }
        Filter.PackagePaths.Add(*SearchPath);
        UE_LOG(LogTemp, Log, TEXT("Searching for materials in: %s"), *SearchPath);
    }
    else
    {
        // Search in common game content locations
        Filter.PackagePaths.Add(TEXT("/Game/"));
        UE_LOG(LogTemp, Log, TEXT("Searching for materials in all game content"));
    }
    
    if (bIncludeEngineMaterials)
    {
        Filter.PackagePaths.Add(TEXT("/Engine/"));
        UE_LOG(LogTemp, Log, TEXT("Including Engine materials in search"));
    }
    
    Filter.bRecursivePaths = true;

    // Get assets from registry
    TArray<FAssetData> AssetDataArray;
    AssetRegistry.GetAssets(Filter, AssetDataArray);
    
    UE_LOG(LogTemp, Log, TEXT("Asset registry found %d materials"), AssetDataArray.Num());

    // Also try manual search using EditorAssetLibrary for more comprehensive results
    TArray<FString> AllAssetPaths;
    if (!SearchPath.IsEmpty())
    {
        AllAssetPaths = UEditorAssetLibrary::ListAssets(SearchPath, true, false);
    }
    else
    {
        AllAssetPaths = UEditorAssetLibrary::ListAssets(TEXT("/Game/"), true, false);
    }
    
    // Filter for materials from the manual search
    for (const FString& AssetPath : AllAssetPaths)
    {
        if (AssetPath.Contains(TEXT("Material")) && !AssetPath.Contains(TEXT(".uasset")))
        {
            UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
            if (Asset && Asset->IsA<UMaterialInterface>())
            {
                // Check if we already have this asset from registry search
                bool bAlreadyFound = false;
                for (const FAssetData& ExistingData : AssetDataArray)
                {
                    if (ExistingData.GetObjectPathString() == AssetPath)
                    {
                        bAlreadyFound = true;
                        break;
                    }
                }
                
                if (!bAlreadyFound)
                {
                    // Create FAssetData manually for this asset
                    FAssetData ManualAssetData(Asset);
                    AssetDataArray.Add(ManualAssetData);
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Total materials found after manual search: %d"), AssetDataArray.Num());

    // Convert to JSON
    TArray<TSharedPtr<FJsonValue>> MaterialArray;
    for (const FAssetData& AssetData : AssetDataArray)
    {
        TSharedPtr<FJsonObject> MaterialObj = MakeShared<FJsonObject>();
        MaterialObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
        MaterialObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        MaterialObj->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
        MaterialObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
        
        MaterialArray.Add(MakeShared<FJsonValueObject>(MaterialObj));
        
        UE_LOG(LogTemp, Verbose, TEXT("Found material: %s at %s"), *AssetData.AssetName.ToString(), *AssetData.GetObjectPathString());
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("materials"), MaterialArray);
    ResultObj->SetNumberField(TEXT("count"), MaterialArray.Num());
    ResultObj->SetStringField(TEXT("search_path_used"), SearchPath.IsEmpty() ? TEXT("/Game/") : SearchPath);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleApplyMaterialToActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Load the material
    UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
    if (!Material)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
    }

    // Find mesh components and apply material
    TArray<UStaticMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UStaticMeshComponent>(MeshComponents);
    
    bool bAppliedToAny = false;
    for (UStaticMeshComponent* MeshComp : MeshComponents)
    {
        if (MeshComp)
        {
            MeshComp->SetMaterial(MaterialSlot, Material);
            bAppliedToAny = true;
        }
    }

    if (!bAppliedToAny)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No mesh components found on actor"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetBoolField(TEXT("success"), true);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleApplyMaterialToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Load the material
    UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
    if (!Material)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
    }

    // Apply the material
    PrimComponent->SetMaterial(MaterialSlot, Material);

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetBoolField(TEXT("success"), true);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetActorMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get mesh components and their materials
    TArray<UStaticMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UStaticMeshComponent>(MeshComponents);
    
    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    
    for (UStaticMeshComponent* MeshComp : MeshComponents)
    {
        if (MeshComp)
        {
            for (int32 i = 0; i < MeshComp->GetNumMaterials(); i++)
            {
                TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();
                SlotInfo->SetNumberField(TEXT("slot"), i);
                SlotInfo->SetStringField(TEXT("component"), MeshComp->GetName());
                
                UMaterialInterface* Material = MeshComp->GetMaterial(i);
                if (Material)
                {
                    SlotInfo->SetStringField(TEXT("material_name"), Material->GetName());
                    SlotInfo->SetStringField(TEXT("material_path"), Material->GetPathName());
                    SlotInfo->SetStringField(TEXT("material_class"), Material->GetClass()->GetName());
                }
                else
                {
                    SlotInfo->SetStringField(TEXT("material_name"), TEXT("None"));
                    SlotInfo->SetStringField(TEXT("material_path"), TEXT(""));
                    SlotInfo->SetStringField(TEXT("material_class"), TEXT(""));
                }
                
                MaterialSlots.Add(MakeShared<FJsonValueObject>(SlotInfo));
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetArrayField(TEXT("material_slots"), MaterialSlots);
    ResultObj->SetNumberField(TEXT("total_slots"), MaterialSlots.Num());
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Get material slot information
    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    int32 NumMaterials = 0;
    
    // Check if we have a static mesh assigned
    UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
    if (StaticMesh)
    {
        NumMaterials = StaticMesh->GetNumSections(0); // Get number of material slots for LOD 0
        
        for (int32 i = 0; i < NumMaterials; i++)
        {
            TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();
            SlotInfo->SetNumberField(TEXT("slot"), i);
            SlotInfo->SetStringField(TEXT("component"), ComponentName);
            
            UMaterialInterface* Material = MeshComponent->GetMaterial(i);
            if (Material)
            {
                SlotInfo->SetStringField(TEXT("material_name"), Material->GetName());
                SlotInfo->SetStringField(TEXT("material_path"), Material->GetPathName());
                SlotInfo->SetStringField(TEXT("material_class"), Material->GetClass()->GetName());
            }
            else
            {
                SlotInfo->SetStringField(TEXT("material_name"), TEXT("None"));
                SlotInfo->SetStringField(TEXT("material_path"), TEXT(""));
                SlotInfo->SetStringField(TEXT("material_class"), TEXT(""));
            }
            
            MaterialSlots.Add(MakeShared<FJsonValueObject>(SlotInfo));
        }
    }
    else
    {
        // If no static mesh is assigned, we can't determine material slots
        UE_LOG(LogTemp, Warning, TEXT("No static mesh assigned to component %s in blueprint %s"), *ComponentName, *BlueprintName);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetArrayField(TEXT("material_slots"), MaterialSlots);
    ResultObj->SetNumberField(TEXT("total_slots"), MaterialSlots.Num());
    ResultObj->SetBoolField(TEXT("has_static_mesh"), StaticMesh != nullptr);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleReadBlueprintContent(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Get optional parameters
    bool bIncludeEventGraph = true;
    bool bIncludeFunctions = true;
    bool bIncludeVariables = true;
    bool bIncludeComponents = true;
    bool bIncludeInterfaces = true;

    Params->TryGetBoolField(TEXT("include_event_graph"), bIncludeEventGraph);
    Params->TryGetBoolField(TEXT("include_functions"), bIncludeFunctions);
    Params->TryGetBoolField(TEXT("include_variables"), bIncludeVariables);
    Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
    Params->TryGetBoolField(TEXT("include_interfaces"), bIncludeInterfaces);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));
    ResultObj->SetStringField(TEXT("parent_class_path"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT(""));
    ResultObj->SetObjectField(TEXT("class_info"), FEpicUnrealMCPCommonUtils::BlueprintClassInfoToJson(Blueprint));

    // Include variables if requested
    if (bIncludeVariables)
    {
        TArray<TSharedPtr<FJsonValue>> VariableArray;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
            VarObj->SetStringField(TEXT("name"), Variable.VarName.ToString());
            VarObj->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
            VarObj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
            VarObj->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
            VariableArray.Add(MakeShared<FJsonValueObject>(VarObj));
        }
        ResultObj->SetArrayField(TEXT("variables"), VariableArray);
    }

    // Include functions if requested
    if (bIncludeFunctions)
    {
        TArray<TSharedPtr<FJsonValue>> FunctionArray;
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph)
            {
                TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
                FuncObj->SetStringField(TEXT("name"), Graph->GetName());
                FuncObj->SetStringField(TEXT("graph_type"), TEXT("Function"));
                
                // Count nodes in function
                int32 NodeCount = Graph->Nodes.Num();
                FuncObj->SetNumberField(TEXT("node_count"), NodeCount);
                
                FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
            }
        }
        ResultObj->SetArrayField(TEXT("functions"), FunctionArray);
    }

    // Include event graph if requested
    if (bIncludeEventGraph)
    {
        TSharedPtr<FJsonObject> EventGraphObj = MakeShared<FJsonObject>();
        
        // Find the main event graph
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph && Graph->GetName() == TEXT("EventGraph"))
            {
                EventGraphObj->SetStringField(TEXT("name"), Graph->GetName());
                EventGraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
                
                // Get basic node information
                TArray<TSharedPtr<FJsonValue>> NodeArray;
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node)
                    {
                        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
                        NodeObj->SetStringField(TEXT("name"), Node->GetName());
                        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
                        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                        NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
                    }
                }
                EventGraphObj->SetArrayField(TEXT("nodes"), NodeArray);
                break;
            }
        }
        
        ResultObj->SetObjectField(TEXT("event_graph"), EventGraphObj);
    }

    // Include components if requested
    if (bIncludeComponents)
    {
        TArray<TSharedPtr<FJsonValue>> ComponentArray;
        if (Blueprint->SimpleConstructionScript)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Node && Node->ComponentTemplate)
                {
                    TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                    CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                    CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
                    CompObj->SetBoolField(TEXT("is_root"), Node == Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode());
                    ComponentArray.Add(MakeShared<FJsonValueObject>(CompObj));
                }
            }
        }
        ResultObj->SetArrayField(TEXT("components"), ComponentArray);
    }

    // Include interfaces if requested
    if (bIncludeInterfaces)
    {
        TArray<TSharedPtr<FJsonValue>> InterfaceArray;
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            TSharedPtr<FJsonObject> InterfaceObj = MakeShared<FJsonObject>();
            InterfaceObj->SetStringField(TEXT("name"), Interface.Interface ? Interface.Interface->GetName() : TEXT("Unknown"));
            InterfaceArray.Add(MakeShared<FJsonValueObject>(InterfaceObj));
        }
        ResultObj->SetArrayField(TEXT("interfaces"), InterfaceArray);
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleReadBlueprintContentFast(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    bool bIncludeEventGraph = true;
    bool bIncludeFunctions = true;
    bool bIncludeVariables = true;
    bool bIncludeComponents = true;
    bool bIncludeInterfaces = true;

    Params->TryGetBoolField(TEXT("include_event_graph"), bIncludeEventGraph);
    Params->TryGetBoolField(TEXT("include_functions"), bIncludeFunctions);
    Params->TryGetBoolField(TEXT("include_variables"), bIncludeVariables);
    Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
    Params->TryGetBoolField(TEXT("include_interfaces"), bIncludeInterfaces);

    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));

    if (bIncludeVariables)
    {
        TArray<TSharedPtr<FJsonValue>> VariableArray;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
            VarObj->SetStringField(TEXT("name"), Variable.VarName.ToString());
            VarObj->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
            VarObj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
            VarObj->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
            VariableArray.Add(MakeShared<FJsonValueObject>(VarObj));
        }

        ResultObj->SetArrayField(TEXT("variables"), VariableArray);
        ResultObj->SetNumberField(TEXT("variable_count"), VariableArray.Num());
    }

    if (bIncludeFunctions)
    {
        TArray<TSharedPtr<FJsonValue>> FunctionArray;
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (!Graph)
            {
                continue;
            }

            TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
            FuncObj->SetStringField(TEXT("name"), Graph->GetName());
            FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
            FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
        }

        ResultObj->SetArrayField(TEXT("functions"), FunctionArray);
        ResultObj->SetNumberField(TEXT("function_count"), FunctionArray.Num());
    }

    if (bIncludeEventGraph)
    {
        TSharedPtr<FJsonObject> EventGraphObj = MakeShared<FJsonObject>();

        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (!Graph || Graph->GetName() != TEXT("EventGraph"))
            {
                continue;
            }

            EventGraphObj->SetStringField(TEXT("name"), Graph->GetName());
            EventGraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

            TArray<TSharedPtr<FJsonValue>> NodeArray;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node)
                {
                    NodeArray.Add(MakeShared<FJsonValueObject>(MakeCompactNodeJson(Node)));
                }
            }

            EventGraphObj->SetArrayField(TEXT("nodes"), NodeArray);
            break;
        }

        ResultObj->SetObjectField(TEXT("event_graph"), EventGraphObj);
    }

    if (bIncludeComponents)
    {
        TArray<TSharedPtr<FJsonValue>> ComponentArray;
        if (Blueprint->SimpleConstructionScript)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (!Node || !Node->ComponentTemplate)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
                CompObj->SetBoolField(TEXT("is_root"), Node == Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode());
                ComponentArray.Add(MakeShared<FJsonValueObject>(CompObj));
            }
        }

        ResultObj->SetArrayField(TEXT("components"), ComponentArray);
        ResultObj->SetNumberField(TEXT("component_count"), ComponentArray.Num());
    }

    if (bIncludeInterfaces)
    {
        TArray<TSharedPtr<FJsonValue>> InterfaceArray;
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            TSharedPtr<FJsonObject> InterfaceObj = MakeShared<FJsonObject>();
            InterfaceObj->SetStringField(TEXT("name"), Interface.Interface ? Interface.Interface->GetName() : TEXT("Unknown"));
            InterfaceArray.Add(MakeShared<FJsonValueObject>(InterfaceObj));
        }

        ResultObj->SetArrayField(TEXT("interfaces"), InterfaceArray);
        ResultObj->SetNumberField(TEXT("interface_count"), InterfaceArray.Num());
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintClassInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> ResultObj = FEpicUnrealMCPCommonUtils::BlueprintClassInfoToJson(Blueprint);
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FBlueprintPropertyQueryOptions QueryOptions;
    BuildPropertyQueryOptionsFromParams_BP(Params, QueryOptions);

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(Blueprint);
    UObject* BlueprintCDO = BlueprintClass ? BlueprintClass->GetDefaultObject() : nullptr;
    if (!BlueprintClass || !BlueprintCDO)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint '%s' has no callable class/default object"), *Blueprint->GetName()));
    }

    TArray<TSharedPtr<FJsonValue>> PropertyArray;
    bool bMatchedRequestedProperty = false;
    CollectObjectProperties_BP(BlueprintCDO, QueryOptions, PropertyArray, bMatchedRequestedProperty);

    if (QueryOptions.RequestedPropertyNames.Num() > 0 && !bMatchedRequestedProperty)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Requested property was not found on the Blueprint default object"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("class_name"), BlueprintClass->GetName());
    ResultObj->SetStringField(TEXT("class_path"), BlueprintClass->GetPathName());
    ResultObj->SetStringField(TEXT("object_path"), BlueprintCDO->GetPathName());
    ResultObj->SetArrayField(TEXT("properties"), PropertyArray);
    ResultObj->SetNumberField(TEXT("property_count"), PropertyArray.Num());
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintComponentProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FBlueprintPropertyQueryOptions QueryOptions;
    BuildPropertyQueryOptionsFromParams_BP(Params, QueryOptions);

    FString RequestedComponentName;
    Params->TryGetStringField(TEXT("component_name"), RequestedComponentName);

    TArray<FBlueprintComponentHandle> Components;
    CollectBlueprintComponents_BP(Blueprint, Components);

    TArray<TSharedPtr<FJsonValue>> ComponentArray;
    bool bFoundRequestedComponent = RequestedComponentName.IsEmpty();

    for (const FBlueprintComponentHandle& Component : Components)
    {
        if (!RequestedComponentName.IsEmpty() && !Component.Name.Equals(RequestedComponentName, ESearchCase::IgnoreCase))
        {
            continue;
        }

        bFoundRequestedComponent = true;

        TArray<TSharedPtr<FJsonValue>> PropertyArray;
        bool bMatchedRequestedProperty = false;
        CollectObjectProperties_BP(Component.Object, QueryOptions, PropertyArray, bMatchedRequestedProperty);

        if (QueryOptions.RequestedPropertyNames.Num() > 0 && !bMatchedRequestedProperty)
        {
            continue;
        }

        TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
        ComponentObject->SetStringField(TEXT("component_name"), Component.Name);
        ComponentObject->SetStringField(TEXT("component_class"), Component.Object->GetClass()->GetName());
        ComponentObject->SetStringField(TEXT("component_class_path"), Component.Object->GetClass()->GetPathName());
        ComponentObject->SetStringField(TEXT("object_path"), Component.Object->GetPathName());
        ComponentObject->SetStringField(TEXT("source"), Component.Source);
        ComponentObject->SetBoolField(TEXT("is_root"), Component.bIsRoot);
        ComponentObject->SetBoolField(TEXT("is_inherited"), Component.bIsInherited);
        ComponentObject->SetArrayField(TEXT("properties"), PropertyArray);
        ComponentObject->SetNumberField(TEXT("property_count"), PropertyArray.Num());
        ComponentArray.Add(MakeShared<FJsonValueObject>(ComponentObject));
    }

    if (!bFoundRequestedComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Component not found: %s"), *RequestedComponentName));
    }

    if (QueryOptions.RequestedPropertyNames.Num() > 0 && ComponentArray.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Requested property was not found on the target component(s)"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

    if (!RequestedComponentName.IsEmpty() && ComponentArray.Num() == 1)
    {
        ResultObj->SetObjectField(TEXT("component"), ComponentArray[0]->AsObject());
    }
    else
    {
        ResultObj->SetArrayField(TEXT("components"), ComponentArray);
        ResultObj->SetNumberField(TEXT("component_count"), ComponentArray.Num());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintPropertiesSpecifiers(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    FString RequestedPropertyName;
    Params->TryGetStringField(TEXT("property_name"), RequestedPropertyName);

    TSet<FString> RequestedPropertyNames;
    if (!RequestedPropertyName.IsEmpty())
    {
        RequestedPropertyNames.Add(RequestedPropertyName);
    }

    const TArray<TSharedPtr<FJsonValue>>* RequestedPropertyArray = nullptr;
    if (Params->TryGetArrayField(TEXT("property_names"), RequestedPropertyArray) && RequestedPropertyArray)
    {
        for (const TSharedPtr<FJsonValue>& PropertyValue : *RequestedPropertyArray)
        {
            FString PropertyNameValue;
            if (PropertyValue.IsValid() && PropertyValue->TryGetString(PropertyNameValue) && !PropertyNameValue.IsEmpty())
            {
                RequestedPropertyNames.Add(PropertyNameValue);
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> SpecifierArray;
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (RequestedPropertyNames.Num() > 0 && !RequestedPropertyNames.Contains(Variable.VarName.ToString()))
        {
            continue;
        }

        TSharedPtr<FJsonObject> VariableObject = MakeShared<FJsonObject>();
        VariableObject->SetStringField(TEXT("name"), Variable.VarName.ToString());
        VariableObject->SetStringField(TEXT("friendly_name"), Variable.FriendlyName.IsEmpty() ? Variable.VarName.ToString() : Variable.FriendlyName);
        VariableObject->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
        VariableObject->SetStringField(TEXT("sub_category"), Variable.VarType.PinSubCategory.ToString());
        VariableObject->SetStringField(TEXT("default_value"), Variable.DefaultValue);
        VariableObject->SetObjectField(TEXT("specifiers"), BuildVariableSpecifierJson_BP(Variable));

        TSharedPtr<FJsonObject> MetadataObject = MakeShared<FJsonObject>();
        if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
        {
            MetadataObject->SetStringField(TEXT("Tooltip"), Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip));
        }
        if (!Variable.Category.IsEmpty())
        {
            MetadataObject->SetStringField(TEXT("Category"), Variable.Category.ToString());
        }
        auto AddVariableMetadata = [&Variable, &MetadataObject](const TCHAR* Key)
        {
            if (Variable.HasMetaData(Key))
            {
                MetadataObject->SetStringField(Key, Variable.GetMetaData(Key));
            }
        };
        AddVariableMetadata(TEXT("ExposeOnSpawn"));
        AddVariableMetadata(TEXT("AllowPrivateAccess"));
        AddVariableMetadata(TEXT("Bitmask"));
        AddVariableMetadata(TEXT("BitmaskEnum"));
        AddVariableMetadata(TEXT("ClampMin"));
        AddVariableMetadata(TEXT("ClampMax"));
        AddVariableMetadata(TEXT("UIMin"));
        AddVariableMetadata(TEXT("UIMax"));
        AddVariableMetadata(TEXT("Units"));
        VariableObject->SetObjectField(TEXT("metadata"), MetadataObject);

        SpecifierArray.Add(MakeShared<FJsonValueObject>(VariableObject));
    }

    if (RequestedPropertyNames.Num() > 0 && SpecifierArray.Num() == 0)
    {
        FString MissingPropertyName;
        if (RequestedPropertyNames.Num() == 1)
        {
            MissingPropertyName = *RequestedPropertyNames.CreateConstIterator();
        }

        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            RequestedPropertyNames.Num() == 1
                ? FString::Printf(TEXT("Blueprint property not found: %s"), *MissingPropertyName)
                : TEXT("None of the requested Blueprint properties were found"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());

    if (RequestedPropertyNames.Num() == 1 && SpecifierArray.Num() == 1)
    {
        ResultObj->SetObjectField(TEXT("property"), SpecifierArray[0]->AsObject());
    }
    else
    {
        ResultObj->SetArrayField(TEXT("properties"), SpecifierArray);
        ResultObj->SetNumberField(TEXT("property_count"), SpecifierArray.Num());
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleEditBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintRef;
    UBlueprint* Blueprint = ResolveBlueprintFromParams_BP(Params, BlueprintRef);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            BlueprintRef.IsEmpty() ? TEXT("Missing blueprint target (target/blueprint_path/blueprint_name)") : FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintRef));
    }

    bool bContinueOnError = false;
    Params->TryGetBoolField(TEXT("continue_on_error"), bContinueOnError);

    bool bCompileBlueprint = true;
    Params->TryGetBoolField(TEXT("compile_blueprint"), bCompileBlueprint);

    bool bHadErrors = false;
    bool bModifiedBlueprint = false;

    TArray<TSharedPtr<FJsonValue>> ErrorArray;
    TArray<TSharedPtr<FJsonValue>> PropertyResults;
    TArray<TSharedPtr<FJsonValue>> ComponentResults;
    TArray<TSharedPtr<FJsonValue>> VariableResults;
    TArray<TSharedPtr<FJsonValue>> FunctionResults;

    auto AddError = [&ErrorArray, &bHadErrors](const FString& Scope, const FString& Message)
    {
        bHadErrors = true;
        ErrorArray.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: %s"), *Scope, *Message)));
    };

    auto ShouldAbort = [&bHadErrors, bContinueOnError]()
    {
        return bHadErrors && !bContinueOnError;
    };

    auto CopyFieldIfPresent = [](const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Target, const TCHAR* SourceField, const TCHAR* TargetField = nullptr)
    {
        if (!Source.IsValid() || !Target.IsValid())
        {
            return;
        }

        const TSharedPtr<FJsonValue> Value = Source->Values.FindRef(SourceField);
        if (Value.IsValid())
        {
            Target->SetField(TargetField ? TargetField : SourceField, Value);
        }
    };

    auto TryGetVariableTypeHint = [](const TSharedPtr<FJsonObject>& VariableSpec, FString& OutType)
    {
        if (!VariableSpec.IsValid())
        {
            return false;
        }

        if (VariableSpec->TryGetStringField(TEXT("variable_type"), OutType) ||
            VariableSpec->TryGetStringField(TEXT("var_type"), OutType) ||
            VariableSpec->TryGetStringField(TEXT("type"), OutType))
        {
            return !OutType.IsEmpty();
        }

        const TSharedPtr<FJsonObject>* TypeHints = nullptr;
        if (VariableSpec->TryGetObjectField(TEXT("type_hints"), TypeHints) && TypeHints && TypeHints->IsValid())
        {
            return (*TypeHints)->TryGetStringField(TEXT("variable_type"), OutType) ||
                (*TypeHints)->TryGetStringField(TEXT("type"), OutType);
        }

        return false;
    };

    auto FindFunctionGraphByName = [Blueprint](const FString& FunctionName) -> UEdGraph*
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph && Graph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
            {
                return Graph;
            }
        }

        return nullptr;
    };

    UClass* BlueprintClass = FEpicUnrealMCPCommonUtils::GetBlueprintCallableClassSafe(Blueprint);
    UObject* BlueprintCDO = BlueprintClass ? BlueprintClass->GetDefaultObject() : nullptr;

    const TSharedPtr<FJsonObject>* BlueprintProperties = nullptr;
    const bool bHasBlueprintProperties =
        BlueprintCDO &&
        ((Params->TryGetObjectField(TEXT("blueprint_properties"), BlueprintProperties) && BlueprintProperties && BlueprintProperties->IsValid()) ||
         (Params->TryGetObjectField(TEXT("properties"), BlueprintProperties) && BlueprintProperties && BlueprintProperties->IsValid()));

    if (bHasBlueprintProperties)
    {
        TArray<TSharedPtr<FJsonValue>> AppliedProperties;
        FString PropertyError;
        if (!ApplyPropertyMapToObject_BP(BlueprintCDO, *BlueprintProperties, AppliedProperties, PropertyError))
        {
            AddError(TEXT("blueprint_properties"), PropertyError);
        }
        else if (AppliedProperties.Num() > 0)
        {
            bModifiedBlueprint = true;

            TSharedPtr<FJsonObject> PropertyResult = MakeShared<FJsonObject>();
            PropertyResult->SetArrayField(TEXT("applied_properties"), AppliedProperties);
            PropertyResult->SetNumberField(TEXT("applied_count"), AppliedProperties.Num());
            PropertyResults.Add(MakeShared<FJsonValueObject>(PropertyResult));
        }
    }

    if (!ShouldAbort())
    {
        const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
        if (Params->TryGetArrayField(TEXT("components"), Components))
        {
            for (int32 Index = 0; Index < Components->Num(); ++Index)
            {
                const TSharedPtr<FJsonValue>& ComponentValue = (*Components)[Index];
                if (!ComponentValue.IsValid() || ComponentValue->Type != EJson::Object)
                {
                    AddError(FString::Printf(TEXT("components[%d]"), Index), TEXT("Component entry must be an object"));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                TSharedPtr<FJsonObject> ComponentSpec = ComponentValue->AsObject();
                FString Operation = TEXT("upsert");
                ComponentSpec->TryGetStringField(TEXT("operation"), Operation);

                FString ComponentName;
                if (!ComponentSpec->TryGetStringField(TEXT("component_name"), ComponentName))
                {
                    ComponentSpec->TryGetStringField(TEXT("name"), ComponentName);
                }

                if (ComponentName.IsEmpty())
                {
                    AddError(FString::Printf(TEXT("components[%d]"), Index), TEXT("Missing component_name"));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                UObject* ExistingComponentObject = ResolveBlueprintEndpointObject(Blueprint, ComponentName);
                const bool bComponentExists = ExistingComponentObject && ExistingComponentObject != BlueprintCDO;

                const bool bShouldCreate =
                    Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase) ||
                    (Operation.Equals(TEXT("upsert"), ESearchCase::IgnoreCase) && !bComponentExists);

                if (bShouldCreate)
                {
                    TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
                    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : ComponentSpec->Values)
                    {
                        CreateParams->SetField(Field.Key, Field.Value);
                    }
                    CreateParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                    CreateParams->SetBoolField(TEXT("compile_blueprint"), false);

                    TSharedPtr<FJsonObject> CreateResult = HandleAddComponentToBlueprint(CreateParams);
                    const bool bCreateSuccess = CreateResult.IsValid() && (!CreateResult->HasField(TEXT("success")) || CreateResult->GetBoolField(TEXT("success")));
                    if (!bCreateSuccess)
                    {
                        AddError(FString::Printf(TEXT("components[%d]"), Index), CreateResult.IsValid() ? CreateResult->GetStringField(TEXT("error")) : TEXT("Failed to create component"));
                        if (ShouldAbort())
                        {
                            break;
                        }
                        continue;
                    }

                    bModifiedBlueprint = true;
                    ComponentResults.Add(MakeShared<FJsonValueObject>(CreateResult));
                    continue;
                }

                if (!bComponentExists)
                {
                    AddError(FString::Printf(TEXT("components[%d]"), Index), FString::Printf(TEXT("Component not found: %s"), *ComponentName));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                TArray<TSharedPtr<FJsonValue>> AppliedFields;
                if (USceneComponent* SceneComponent = Cast<USceneComponent>(ExistingComponentObject))
                {
                    ApplySceneComponentOverrides_BP(SceneComponent, ComponentSpec, AppliedFields);
                }

                const TSharedPtr<FJsonObject>* ComponentProperties = nullptr;
                TArray<TSharedPtr<FJsonValue>> AppliedProperties;
                FString ComponentError;
                if (ComponentSpec->TryGetObjectField(TEXT("component_properties"), ComponentProperties) && ComponentProperties && ComponentProperties->IsValid())
                {
                    if (!ApplyPropertyMapToObject_BP(ExistingComponentObject, *ComponentProperties, AppliedProperties, ComponentError))
                    {
                        AddError(FString::Printf(TEXT("components[%d]"), Index), ComponentError);
                        if (ShouldAbort())
                        {
                            break;
                        }
                        continue;
                    }
                }

                if (AppliedFields.Num() > 0 || AppliedProperties.Num() > 0)
                {
                    bModifiedBlueprint = true;
                    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

                    TSharedPtr<FJsonObject> ComponentResult = MakeShared<FJsonObject>();
                    ComponentResult->SetStringField(TEXT("component_name"), ComponentName);
                    ComponentResult->SetStringField(TEXT("operation"), TEXT("edit"));
                    ComponentResult->SetArrayField(TEXT("transform_fields_applied"), AppliedFields);
                    ComponentResult->SetArrayField(TEXT("component_properties_applied"), AppliedProperties);
                    ComponentResult->SetNumberField(TEXT("component_properties_applied_count"), AppliedProperties.Num());
                    ComponentResults.Add(MakeShared<FJsonValueObject>(ComponentResult));
                }
            }
        }
    }

    if (!ShouldAbort())
    {
        const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
        if (Params->TryGetArrayField(TEXT("variables"), Variables))
        {
            for (int32 Index = 0; Index < Variables->Num(); ++Index)
            {
                const TSharedPtr<FJsonValue>& VariableValue = (*Variables)[Index];
                if (!VariableValue.IsValid() || VariableValue->Type != EJson::Object)
                {
                    AddError(FString::Printf(TEXT("variables[%d]"), Index), TEXT("Variable entry must be an object"));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                TSharedPtr<FJsonObject> VariableSpec = VariableValue->AsObject();
                FString Operation = TEXT("upsert");
                VariableSpec->TryGetStringField(TEXT("operation"), Operation);

                FString VariableName;
                if (!VariableSpec->TryGetStringField(TEXT("variable_name"), VariableName))
                {
                    VariableSpec->TryGetStringField(TEXT("name"), VariableName);
                }

                if (VariableName.IsEmpty())
                {
                    AddError(FString::Printf(TEXT("variables[%d]"), Index), TEXT("Missing variable_name"));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                const bool bVariableExists = BlueprintHasVariable_BP(Blueprint, VariableName);
                if (Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase) && bVariableExists)
                {
                    AddError(FString::Printf(TEXT("variables[%d]"), Index), FString::Printf(TEXT("Variable already exists: %s"), *VariableName));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                if ((Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase) || Operation.Equals(TEXT("upsert"), ESearchCase::IgnoreCase)) && !bVariableExists)
                {
                    FString VariableType;
                    if (!TryGetVariableTypeHint(VariableSpec, VariableType))
                    {
                        AddError(FString::Printf(TEXT("variables[%d]"), Index), FString::Printf(TEXT("Missing variable_type for new variable: %s"), *VariableName));
                        if (ShouldAbort())
                        {
                            break;
                        }
                        continue;
                    }

                    TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
                    CreateParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                    CreateParams->SetStringField(TEXT("variable_name"), VariableName);
                    CreateParams->SetStringField(TEXT("variable_type"), VariableType);
                    CopyFieldIfPresent(VariableSpec, CreateParams, TEXT("default_value"));
                    CopyFieldIfPresent(VariableSpec, CreateParams, TEXT("tooltip"));
                    CopyFieldIfPresent(VariableSpec, CreateParams, TEXT("category"));

                    const TSharedPtr<FJsonObject>* VariableSpecifiers = nullptr;
                    if (VariableSpec->TryGetObjectField(TEXT("specifiers"), VariableSpecifiers) && VariableSpecifiers && VariableSpecifiers->IsValid())
                    {
                        CopyFieldIfPresent(*VariableSpecifiers, CreateParams, TEXT("is_public"));
                    }
                    else
                    {
                        CopyFieldIfPresent(VariableSpec, CreateParams, TEXT("is_public"));
                    }

                    TSharedPtr<FJsonObject> CreateResult = FBPVariables::CreateVariable(CreateParams);
                    const bool bCreateSuccess = CreateResult.IsValid() && CreateResult->GetBoolField(TEXT("success"));
                    if (!bCreateSuccess)
                    {
                        AddError(FString::Printf(TEXT("variables[%d]"), Index), CreateResult.IsValid() ? CreateResult->GetStringField(TEXT("error")) : TEXT("Failed to create variable"));
                        if (ShouldAbort())
                        {
                            break;
                        }
                        continue;
                    }

                    bModifiedBlueprint = true;
                    VariableResults.Add(MakeShared<FJsonValueObject>(CreateResult));
                }

                if (!BlueprintHasVariable_BP(Blueprint, VariableName))
                {
                    AddError(FString::Printf(TEXT("variables[%d]"), Index), FString::Printf(TEXT("Variable not found: %s"), *VariableName));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                TSharedPtr<FJsonObject> UpdateParams = MakeShared<FJsonObject>();
                UpdateParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                UpdateParams->SetStringField(TEXT("variable_name"), VariableName);

                FString VariableType;
                if (TryGetVariableTypeHint(VariableSpec, VariableType))
                {
                    UpdateParams->SetStringField(TEXT("var_type"), VariableType);
                }

                const TSharedPtr<FJsonObject>* Renames = nullptr;
                if (VariableSpec->TryGetObjectField(TEXT("renames"), Renames) && Renames && Renames->IsValid())
                {
                    FString NewVariableName;
                    if ((*Renames)->TryGetStringField(TEXT("new_name"), NewVariableName) || (*Renames)->TryGetStringField(TEXT("variable_name"), NewVariableName))
                    {
                        UpdateParams->SetStringField(TEXT("var_name"), NewVariableName);
                    }
                }

                CopyFieldIfPresent(VariableSpec, UpdateParams, TEXT("default_value"));
                CopyFieldIfPresent(VariableSpec, UpdateParams, TEXT("tooltip"));
                CopyFieldIfPresent(VariableSpec, UpdateParams, TEXT("category"));
                CopyFieldIfPresent(VariableSpec, UpdateParams, TEXT("friendly_name"));

                const TSharedPtr<FJsonObject>* VariableSpecifiers = nullptr;
                if (VariableSpec->TryGetObjectField(TEXT("specifiers"), VariableSpecifiers) && VariableSpecifiers && VariableSpecifiers->IsValid())
                {
                    const TArray<FString> SpecifierFields = {
                        TEXT("is_public"), TEXT("is_blueprint_writable"), TEXT("is_editable_in_instance"),
                        TEXT("is_config"), TEXT("replication_enabled"), TEXT("replication_condition"),
                        TEXT("is_private"), TEXT("expose_on_spawn"), TEXT("expose_to_cinematics"),
                        TEXT("slider_range_min"), TEXT("slider_range_max"), TEXT("value_range_min"),
                        TEXT("value_range_max"), TEXT("units"), TEXT("bitmask"), TEXT("bitmask_enum")
                    };

                    for (const FString& FieldName : SpecifierFields)
                    {
                        CopyFieldIfPresent(*VariableSpecifiers, UpdateParams, *FieldName);
                    }
                }

                if (UpdateParams->Values.Num() > 2)
                {
                    TSharedPtr<FJsonObject> UpdateResult = FBPVariables::SetVariableProperties(UpdateParams);
                    const bool bUpdateSuccess = UpdateResult.IsValid() && UpdateResult->GetBoolField(TEXT("success"));
                    if (!bUpdateSuccess)
                    {
                        AddError(FString::Printf(TEXT("variables[%d]"), Index), UpdateResult.IsValid() ? UpdateResult->GetStringField(TEXT("error")) : TEXT("Failed to update variable"));
                        if (ShouldAbort())
                        {
                            break;
                        }
                        continue;
                    }

                    bModifiedBlueprint = true;
                    VariableResults.Add(MakeShared<FJsonValueObject>(UpdateResult));
                }
            }
        }
    }

    if (!ShouldAbort())
    {
        const TArray<TSharedPtr<FJsonValue>>* Functions = nullptr;
        if (Params->TryGetArrayField(TEXT("functions"), Functions))
        {
            for (int32 Index = 0; Index < Functions->Num(); ++Index)
            {
                const TSharedPtr<FJsonValue>& FunctionValue = (*Functions)[Index];
                if (!FunctionValue.IsValid() || FunctionValue->Type != EJson::Object)
                {
                    AddError(FString::Printf(TEXT("functions[%d]"), Index), TEXT("Function entry must be an object"));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                TSharedPtr<FJsonObject> FunctionSpec = FunctionValue->AsObject();
                FString Operation = TEXT("upsert");
                FunctionSpec->TryGetStringField(TEXT("operation"), Operation);

                FString FunctionName;
                if (!FunctionSpec->TryGetStringField(TEXT("function_name"), FunctionName))
                {
                    FunctionSpec->TryGetStringField(TEXT("name"), FunctionName);
                }

                if (FunctionName.IsEmpty())
                {
                    AddError(FString::Printf(TEXT("functions[%d]"), Index), TEXT("Missing function_name"));
                    if (ShouldAbort())
                    {
                        break;
                    }
                    continue;
                }

                FString CurrentFunctionName = FunctionName;

                if (Operation.Equals(TEXT("delete"), ESearchCase::IgnoreCase))
                {
                    TSharedPtr<FJsonObject> DeleteParams = MakeShared<FJsonObject>();
                    DeleteParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                    DeleteParams->SetStringField(TEXT("function_name"), CurrentFunctionName);
                    TSharedPtr<FJsonObject> DeleteResult = FFunctionManager::DeleteFunction(DeleteParams);
                    const bool bDeleteSuccess = DeleteResult.IsValid() && DeleteResult->GetBoolField(TEXT("success"));
                    if (!bDeleteSuccess)
                    {
                        AddError(FString::Printf(TEXT("functions[%d]"), Index), DeleteResult.IsValid() ? DeleteResult->GetStringField(TEXT("error")) : TEXT("Failed to delete function"));
                        if (ShouldAbort())
                        {
                            break;
                        }
                        continue;
                    }

                    bModifiedBlueprint = true;
                    FunctionResults.Add(MakeShared<FJsonValueObject>(DeleteResult));
                    continue;
                }

                if ((Operation.Equals(TEXT("create"), ESearchCase::IgnoreCase) || Operation.Equals(TEXT("upsert"), ESearchCase::IgnoreCase)) &&
                    !FindFunctionGraphByName(CurrentFunctionName))
                {
                    TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
                    CreateParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                    CreateParams->SetStringField(TEXT("function_name"), CurrentFunctionName);
                    CopyFieldIfPresent(FunctionSpec, CreateParams, TEXT("return_type"));

                    TSharedPtr<FJsonObject> CreateResult = FFunctionManager::CreateFunction(CreateParams);
                    const bool bCreateSuccess = CreateResult.IsValid() && CreateResult->GetBoolField(TEXT("success"));
                    if (!bCreateSuccess)
                    {
                        AddError(FString::Printf(TEXT("functions[%d]"), Index), CreateResult.IsValid() ? CreateResult->GetStringField(TEXT("error")) : TEXT("Failed to create function"));
                        if (ShouldAbort())
                        {
                            break;
                        }
                        continue;
                    }

                    bModifiedBlueprint = true;
                    FunctionResults.Add(MakeShared<FJsonValueObject>(CreateResult));
                }

                const TSharedPtr<FJsonObject>* Renames = nullptr;
                if (FunctionSpec->TryGetObjectField(TEXT("renames"), Renames) && Renames && Renames->IsValid())
                {
                    FString NewFunctionName;
                    if ((*Renames)->TryGetStringField(TEXT("new_name"), NewFunctionName) && !NewFunctionName.IsEmpty() && !NewFunctionName.Equals(CurrentFunctionName, ESearchCase::IgnoreCase))
                    {
                        TSharedPtr<FJsonObject> RenameParams = MakeShared<FJsonObject>();
                        RenameParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                        RenameParams->SetStringField(TEXT("old_function_name"), CurrentFunctionName);
                        RenameParams->SetStringField(TEXT("new_function_name"), NewFunctionName);
                        TSharedPtr<FJsonObject> RenameResult = FFunctionManager::RenameFunction(RenameParams);
                        const bool bRenameSuccess = RenameResult.IsValid() && RenameResult->GetBoolField(TEXT("success"));
                        if (!bRenameSuccess)
                        {
                            AddError(FString::Printf(TEXT("functions[%d]"), Index), RenameResult.IsValid() ? RenameResult->GetStringField(TEXT("error")) : TEXT("Failed to rename function"));
                            if (ShouldAbort())
                            {
                                break;
                            }
                            continue;
                        }

                        CurrentFunctionName = NewFunctionName;
                        bModifiedBlueprint = true;
                        FunctionResults.Add(MakeShared<FJsonValueObject>(RenameResult));
                    }
                }

                const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
                if (FunctionSpec->TryGetArrayField(TEXT("inputs"), Inputs))
                {
                    for (const TSharedPtr<FJsonValue>& InputValue : *Inputs)
                    {
                        if (!InputValue.IsValid() || InputValue->Type != EJson::Object)
                        {
                            AddError(FString::Printf(TEXT("functions[%d]"), Index), TEXT("Function input entry must be an object"));
                            break;
                        }

                        TSharedPtr<FJsonObject> InputSpec = InputValue->AsObject();
                        TSharedPtr<FJsonObject> InputParams = MakeShared<FJsonObject>();
                        InputParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                        InputParams->SetStringField(TEXT("function_name"), CurrentFunctionName);
                        CopyFieldIfPresent(InputSpec, InputParams, TEXT("param_name"));
                        CopyFieldIfPresent(InputSpec, InputParams, TEXT("name"), TEXT("param_name"));
                        CopyFieldIfPresent(InputSpec, InputParams, TEXT("param_type"));
                        CopyFieldIfPresent(InputSpec, InputParams, TEXT("type"), TEXT("param_type"));
                        CopyFieldIfPresent(InputSpec, InputParams, TEXT("is_array"));
                        InputParams->SetStringField(TEXT("direction"), TEXT("input"));

                        TSharedPtr<FJsonObject> InputResult = FFunctionIO::AddFunctionIO(InputParams);
                        const bool bInputSuccess = InputResult.IsValid() && InputResult->GetBoolField(TEXT("success"));
                        if (!bInputSuccess)
                        {
                            AddError(FString::Printf(TEXT("functions[%d]"), Index), InputResult.IsValid() ? InputResult->GetStringField(TEXT("error")) : TEXT("Failed to add input parameter"));
                            break;
                        }

                        bModifiedBlueprint = true;
                        FunctionResults.Add(MakeShared<FJsonValueObject>(InputResult));
                    }
                }

                if (ShouldAbort())
                {
                    break;
                }

                const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
                if (FunctionSpec->TryGetArrayField(TEXT("outputs"), Outputs))
                {
                    for (const TSharedPtr<FJsonValue>& OutputValue : *Outputs)
                    {
                        if (!OutputValue.IsValid() || OutputValue->Type != EJson::Object)
                        {
                            AddError(FString::Printf(TEXT("functions[%d]"), Index), TEXT("Function output entry must be an object"));
                            break;
                        }

                        TSharedPtr<FJsonObject> OutputSpec = OutputValue->AsObject();
                        TSharedPtr<FJsonObject> OutputParams = MakeShared<FJsonObject>();
                        OutputParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
                        OutputParams->SetStringField(TEXT("function_name"), CurrentFunctionName);
                        CopyFieldIfPresent(OutputSpec, OutputParams, TEXT("param_name"));
                        CopyFieldIfPresent(OutputSpec, OutputParams, TEXT("name"), TEXT("param_name"));
                        CopyFieldIfPresent(OutputSpec, OutputParams, TEXT("param_type"));
                        CopyFieldIfPresent(OutputSpec, OutputParams, TEXT("type"), TEXT("param_type"));
                        CopyFieldIfPresent(OutputSpec, OutputParams, TEXT("is_array"));
                        OutputParams->SetStringField(TEXT("direction"), TEXT("output"));

                        TSharedPtr<FJsonObject> OutputResult = FFunctionIO::AddFunctionIO(OutputParams);
                        const bool bOutputSuccess = OutputResult.IsValid() && OutputResult->GetBoolField(TEXT("success"));
                        if (!bOutputSuccess)
                        {
                            AddError(FString::Printf(TEXT("functions[%d]"), Index), OutputResult.IsValid() ? OutputResult->GetStringField(TEXT("error")) : TEXT("Failed to add output parameter"));
                            break;
                        }

                        bModifiedBlueprint = true;
                        FunctionResults.Add(MakeShared<FJsonValueObject>(OutputResult));
                    }
                }

                if (ShouldAbort())
                {
                    break;
                }
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetBoolField(TEXT("modified"), bModifiedBlueprint);

    if (bCompileBlueprint && bModifiedBlueprint)
    {
        TSharedPtr<FJsonObject> CompileParams = MakeShared<FJsonObject>();
        CompileParams->SetStringField(TEXT("blueprint_name"), Blueprint->GetPathName());
        TSharedPtr<FJsonObject> CompileResult = HandleCompileBlueprint(CompileParams);
        ResultObj->SetObjectField(TEXT("compile_result"), CompileResult);

        const bool bCompileSucceeded = CompileResult.IsValid() && CompileResult->GetBoolField(TEXT("compiled"));
        if (!bCompileSucceeded)
        {
            AddError(TEXT("compile_blueprint"), TEXT("Blueprint compile reported errors"));
        }
    }

    ResultObj->SetArrayField(TEXT("property_updates"), PropertyResults);
    ResultObj->SetArrayField(TEXT("component_results"), ComponentResults);
    ResultObj->SetArrayField(TEXT("variable_results"), VariableResults);
    ResultObj->SetArrayField(TEXT("function_results"), FunctionResults);
    ResultObj->SetArrayField(TEXT("errors"), ErrorArray);
    ResultObj->SetNumberField(TEXT("error_count"), ErrorArray.Num());
    ResultObj->SetBoolField(TEXT("success"), ErrorArray.Num() == 0);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAnalyzeBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString GraphName = TEXT("EventGraph");
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    // Get optional parameters
    bool bIncludeNodeDetails = true;
    bool bIncludePinConnections = true;
    bool bTraceExecutionFlow = true;

    Params->TryGetBoolField(TEXT("include_node_details"), bIncludeNodeDetails);
    Params->TryGetBoolField(TEXT("include_pin_connections"), bIncludePinConnections);
    Params->TryGetBoolField(TEXT("trace_execution_flow"), bTraceExecutionFlow);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    // Find the specified graph. Use full graph enumeration so nested animation graphs
    // (e.g. AnimationBlendStackGraph_*) can be resolved.
    UEdGraph* TargetGraph = FindBlueprintGraphByNameOrPath(Blueprint, GraphName);

    if (!TargetGraph)
    {
        const FString AvailableGraphList = FString::Join(GetAllBlueprintGraphNames(Blueprint), TEXT(", "));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Graph not found: %s. Available graphs: %s"), *GraphName, *AvailableGraphList));
    }

    TSharedPtr<FJsonObject> GraphData = MakeShared<FJsonObject>();
    GraphData->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
    GraphData->SetStringField(TEXT("graph_type"), TargetGraph->GetClass()->GetName());

    // Analyze nodes
    TArray<TSharedPtr<FJsonValue>> NodeArray;
    TArray<TSharedPtr<FJsonValue>> ConnectionArray;

    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node)
        {
            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("name"), Node->GetName());
            NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
            NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

            if (bIncludeNodeDetails)
            {
                NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
                NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
                NodeObj->SetBoolField(TEXT("can_rename"), Node->bCanRenameNode);
            }

            // Include pin information if requested
            if (bIncludePinConnections)
            {
                TArray<TSharedPtr<FJsonValue>> PinArray;
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin)
                    {
                        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                        PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                        PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                        PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
                        PinObj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());
                        
                        // Record connections for this pin
                        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                        {
                            if (LinkedPin && LinkedPin->GetOwningNode())
                            {
                                TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                                ConnObj->SetStringField(TEXT("from_node"), Pin->GetOwningNode()->GetName());
                                ConnObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
                                ConnObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->GetName());
                                ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
                                ConnectionArray.Add(MakeShared<FJsonValueObject>(ConnObj));
                            }
                        }
                        
                        PinArray.Add(MakeShared<FJsonValueObject>(PinObj));
                    }
                }
                NodeObj->SetArrayField(TEXT("pins"), PinArray);
            }

            NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
    }

    GraphData->SetArrayField(TEXT("nodes"), NodeArray);
    GraphData->SetArrayField(TEXT("connections"), ConnectionArray);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetObjectField(TEXT("graph_data"), GraphData);
    ResultObj->SetBoolField(TEXT("success"), true);

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAnalyzeBlueprintGraphFast(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString GraphName = TEXT("EventGraph");
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    bool bIncludePinConnections = true;
    Params->TryGetBoolField(TEXT("include_pin_connections"), bIncludePinConnections);

    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    UEdGraph* TargetGraph = FindBlueprintGraphByNameOrPath(Blueprint, GraphName);
    if (!TargetGraph)
    {
        const FString AvailableGraphList = FString::Join(GetAllBlueprintGraphNames(Blueprint), TEXT(", "));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Graph not found: %s. Available graphs: %s"), *GraphName, *AvailableGraphList));
    }

    TSharedPtr<FJsonObject> GraphData = MakeShared<FJsonObject>();
    GraphData->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
    GraphData->SetStringField(TEXT("graph_type"), TargetGraph->GetClass()->GetName());

    TArray<TSharedPtr<FJsonValue>> NodeArray;
    TArray<TSharedPtr<FJsonValue>> ConnectionArray;
    TSet<FString> SeenConnections;

    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (!Node)
        {
            continue;
        }

        NodeArray.Add(MakeShared<FJsonValueObject>(MakeCompactNodeJson(Node)));

        if (!bIncludePinConnections)
        {
            continue;
        }

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                AddUniqueCompactConnection(Pin, LinkedPin, ConnectionArray, SeenConnections);
            }
        }
    }

    GraphData->SetArrayField(TEXT("nodes"), NodeArray);
    GraphData->SetNumberField(TEXT("node_count"), NodeArray.Num());
    GraphData->SetArrayField(TEXT("connections"), ConnectionArray);
    GraphData->SetNumberField(TEXT("connection_count"), ConnectionArray.Num());

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetObjectField(TEXT("graph_data"), GraphData);
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintVariableDetails(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString VariableName;
    bool bSpecificVariable = Params->TryGetStringField(TEXT("variable_name"), VariableName);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TArray<TSharedPtr<FJsonValue>> VariableArray;

    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        // If looking for specific variable, skip others
        if (bSpecificVariable && Variable.VarName.ToString() != VariableName)
        {
            continue;
        }

        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Variable.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
        VarObj->SetStringField(TEXT("sub_category"), Variable.VarType.PinSubCategory.ToString());
        VarObj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
        VarObj->SetStringField(TEXT("friendly_name"), Variable.FriendlyName.IsEmpty() ? Variable.VarName.ToString() : Variable.FriendlyName);
        
        // Get tooltip from metadata (VarTooltip doesn't exist in UE 5.5)
        FString TooltipValue;
        if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
        {
            TooltipValue = Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip);
        }
        VarObj->SetStringField(TEXT("tooltip"), TooltipValue);
        
        VarObj->SetStringField(TEXT("category"), Variable.Category.ToString());

        // Property flags
        VarObj->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
        VarObj->SetBoolField(TEXT("is_blueprint_visible"), (Variable.PropertyFlags & CPF_BlueprintVisible) != 0);
        VarObj->SetBoolField(TEXT("is_editable_in_instance"), (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0);
        VarObj->SetBoolField(TEXT("is_config"), (Variable.PropertyFlags & CPF_Config) != 0);

        // Replication
        VarObj->SetNumberField(TEXT("replication"), (int32)Variable.ReplicationCondition);

        VariableArray.Add(MakeShared<FJsonValueObject>(VarObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    
    if (bSpecificVariable)
    {
        ResultObj->SetStringField(TEXT("variable_name"), VariableName);
        if (VariableArray.Num() > 0)
        {
            ResultObj->SetObjectField(TEXT("variable"), VariableArray[0]->AsObject());
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: %s"), *VariableName));
        }
    }
    else
    {
        ResultObj->SetArrayField(TEXT("variables"), VariableArray);
        ResultObj->SetNumberField(TEXT("variable_count"), VariableArray.Num());
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintFunctionDetails(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString FunctionName;
    bool bSpecificFunction = Params->TryGetStringField(TEXT("function_name"), FunctionName);

    bool bIncludeGraph = true;
    Params->TryGetBoolField(TEXT("include_graph"), bIncludeGraph);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TArray<TSharedPtr<FJsonValue>> FunctionArray;

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph) continue;

        // If looking for specific function, skip others
        if (bSpecificFunction && Graph->GetName() != FunctionName)
        {
            continue;
        }

        TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
        FuncObj->SetStringField(TEXT("name"), Graph->GetName());
        FuncObj->SetStringField(TEXT("graph_type"), TEXT("Function"));

        // Get function signature from graph
        TArray<TSharedPtr<FJsonValue>> InputPins;
        TArray<TSharedPtr<FJsonValue>> OutputPins;

        // Find function entry and result nodes
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node)
            {
                if (Node->GetClass()->GetName().Contains(TEXT("FunctionEntry")))
                {
                    // Process input parameters
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != TEXT("then"))
                        {
                            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                            InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
                        }
                    }
                }
                else if (Node->GetClass()->GetName().Contains(TEXT("FunctionResult")))
                {
                    // Process output parameters
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Input && Pin->PinName != TEXT("exec"))
                        {
                            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                            OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
                        }
                    }
                }
            }
        }

        FuncObj->SetArrayField(TEXT("input_parameters"), InputPins);
        FuncObj->SetArrayField(TEXT("output_parameters"), OutputPins);
        FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        // Include graph details if requested
        if (bIncludeGraph)
        {
            TArray<TSharedPtr<FJsonValue>> NodeArray;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node)
                {
                    TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
                    NodeObj->SetStringField(TEXT("name"), Node->GetName());
                    NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
                    NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                    NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
                }
            }
            FuncObj->SetArrayField(TEXT("graph_nodes"), NodeArray);
        }

        FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    
    if (bSpecificFunction)
    {
        ResultObj->SetStringField(TEXT("function_name"), FunctionName);
        if (FunctionArray.Num() > 0)
        {
            ResultObj->SetObjectField(TEXT("function"), FunctionArray[0]->AsObject());
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
        }
    }
    else
    {
        ResultObj->SetArrayField(TEXT("functions"), FunctionArray);
        ResultObj->SetNumberField(TEXT("function_count"), FunctionArray.Num());
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}
